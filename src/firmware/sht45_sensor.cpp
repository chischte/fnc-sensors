#include "firmware/sht45_sensor.h"

#include "config.h"
#include "firmware/time_utils.h"

namespace {
constexpr uint8_t MEASURE_HIGH_PRECISION = 0xFD;
constexpr uint8_t HEAT_200_MW_ONE_SECOND = 0x39;
constexpr uint32_t CONVERSION_TIME_MS = 10;
// Datasheet maximum for the nominal 1 s heater pulse is 1.1 s.
constexpr uint32_t HEATER_CONVERSION_TIME_MS = 1100 + CONVERSION_TIME_MS;
static_assert(Config::SHT45_HEATER_INTERVAL_MS >=
                  HEATER_CONVERSION_TIME_MS + Config::SHT45_HEATER_COOLDOWN_MS,
              "Heater interval must include the complete cooldown");
// Sampling and conversion run independently; allow the next conversion to finish.
constexpr uint32_t MAX_READING_AGE_MS = 2 * Config::SHT45_SAMPLE_INTERVAL_MS;
constexpr uint8_t RESPONSE_BYTES = 6;
constexpr uint8_t CRC_POLYNOMIAL = 0x31;

bool checksumValid(const uint8_t* word) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < 2; ++i) {
    crc ^= word[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? (crc << 1) ^ CRC_POLYNOMIAL : crc << 1;
    }
  }
  return crc == word[2];
}
}  // namespace

void Sht45Sensor::begin(uint32_t now) {
  Wire.begin();
  Wire.setClock(Config::I2C_CLOCK_HZ);
  Wire1.begin();
  Wire1.setClock(Config::I2C_CLOCK_HZ);
  lastAttemptAt_ = now - Config::SHT45_SAMPLE_INTERVAL_MS;
  lastHeaterStartedAt_ = now;
  poll(now);
}

void Sht45Sensor::poll(uint32_t now) {
  if (pending_) {
    const uint32_t waitMs = heating_ ? HEATER_CONVERSION_TIME_MS : CONVERSION_TIME_MS;
    if (hasElapsed(now, lastAttemptAt_, waitMs)) {
      finishMeasurement(now);
    }
    return;
  }
  if (hasHeated_ && !hasElapsed(now, lastHeaterStartedAt_,
      HEATER_CONVERSION_TIME_MS + Config::SHT45_HEATER_COOLDOWN_MS)) {
    return;
  }
  if (!heaterDue(now) &&
      !hasElapsed(now, lastAttemptAt_, Config::SHT45_SAMPLE_INTERVAL_MS)) {
    return;
  }
  lastAttemptAt_ = now;
  if (bus_ && startMeasurement(*bus_, now)) {
    return;
  }
  valid_ = false;
  bus_ = nullptr;
  if (!startMeasurement(Wire, now)) {
    startMeasurement(Wire1, now);
  }
}

bool Sht45Sensor::startMeasurement(TwoWire& bus, uint32_t now) {
  const bool heat = heaterDue(now);
  bus.beginTransmission(Config::SHT45_I2C_ADDRESS);
  bus.write(heat ? HEAT_200_MW_ONE_SECOND : MEASURE_HIGH_PRECISION);
  if (bus.endTransmission() != 0) {
    return false;
  }
  bus_ = &bus;
  pending_ = true;
  heating_ = heat;
  if (heat) {
    lastHeaterStartedAt_ = now;
    hasHeated_ = true;
    heaterRequested_ = false;
  }
  lastAttemptAt_ = now;
  return true;
}

void Sht45Sensor::finishMeasurement(uint32_t now) {
  pending_ = false;
  valid_ = false;
  uint8_t data[RESPONSE_BYTES];
  if (bus_->requestFrom(Config::SHT45_I2C_ADDRESS, RESPONSE_BYTES) != RESPONSE_BYTES) {
    bus_ = nullptr;
    return;
  }
  for (uint8_t i = 0; i < RESPONSE_BYTES; ++i) {
    data[i] = bus_->read();
  }
  if (!checksumValid(data) || !checksumValid(data + 3)) {
    bus_ = nullptr;
    return;
  }
  // Conversion and saturation follow the SHT4x datasheet, section 4.6.
  const uint16_t temperatureTicks = (data[0] << 8) | data[1];
  const uint16_t humidityTicks = (data[3] << 8) | data[4];
  temperature_ = -45.0f + 175.0f * temperatureTicks / 65535.0f;
  humidity_ = -6.0f + 125.0f * humidityTicks / 65535.0f;
  if (humidity_ < Config::HUMIDITY_MIN_RH) humidity_ = Config::HUMIDITY_MIN_RH;
  if (humidity_ > Config::HUMIDITY_MAX_RH) humidity_ = Config::HUMIDITY_MAX_RH;
  lastReadingAt_ = now;
  heatedReading_ = heating_;
  valid_ = true;
}

bool Sht45Sensor::heaterDue(uint32_t now) const {
  return Config::SHT45_HEATER_ENABLED &&
      heaterRequested_ && valid_ && !heatedReading_ &&
      !hasElapsed(now, lastReadingAt_, MAX_READING_AGE_MS) &&
      temperature_ < Config::SHT45_HEATER_MAX_AMBIENT_C &&
      hasElapsed(now, lastHeaterStartedAt_, Config::SHT45_HEATER_INTERVAL_MS);
}

bool Sht45Sensor::heatedReading() const {
  return heatedReading_;
}

bool Sht45Sensor::coolingReading() const {
  return hasHeated_ && !heatedReading_ &&
      !hasElapsed(lastReadingAt_, lastHeaterStartedAt_,
                  HEATER_CONVERSION_TIME_MS + Config::SHT45_HEATER_COOLDOWN_MS);
}

bool Sht45Sensor::read(float& temperature, float& humidity, uint32_t now) {
  if (heatedReading_ || coolingReading() || !valid_ ||
      hasElapsed(now, lastReadingAt_, MAX_READING_AGE_MS)) {
    temperature = NAN;
    humidity = NAN;
    return false;
  }
  // Do not queue the first pulse for a later point inside the next sample window.
  heaterRequested_ = hasHeated_ ||
      hasElapsed(now, lastHeaterStartedAt_, Config::SHT45_HEATER_INTERVAL_MS);
  temperature = temperature_;
  humidity = humidity_;
  return true;
}

int32_t Sht45Sensor::heaterElapsedMs() const {
  if (!hasHeated_ || heatedReading_ || coolingReading()) return -1;
  return static_cast<int32_t>(lastReadingAt_ - lastHeaterStartedAt_ -
                              HEATER_CONVERSION_TIME_MS);
}
