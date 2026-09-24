#define assert(condition) do { if (!(condition)) return __LINE__; } while (false)
#ifdef _WIN32
extern "C" { int _fltused = 0; }
#endif
#include "config.h"
#include <Arduino_PortentaMachineControl.h>
#include "firmware/sensor_manager.h"

void setRtdFilter50Hz(bool) {}
void setRtdLeadCompensation(bool) {}
uint16_t lastRtdRawCount() { return 9000; }
RtdDriverDiagnostics lastRtdDiagnostics() { return {0x11, 0x11, true, 0}; }

void finishInitialization(SensorManager& sensor, uint32_t started) {
  sensor.poll(started + Config::SCD_WAKE_DELAY_MS);
  sensor.poll(started + Config::SCD_WAKE_DELAY_MS + Config::SCD_STOP_DELAY_MS);
}

int main() {
  SensorManager sensor;
  sensor.begin(0);
  finishInitialization(sensor, 0);
  assert(sensor.isReady());
  Measurement original;
  sensor.read(original, 6000);
  assert(original.co2Valid && original.scdSerialNumber == 1);

  // A quick swap can produce no I2C errors: the new sensor acknowledges in idle.
  fakeScd.serial = 2;
  fakeScd.measuring = false;
  Measurement missing;
  sensor.read(missing, 11000);
  assert(!missing.co2Valid);
  sensor.poll(6000 + Config::SCD_MEASUREMENT_TIMEOUT_MS - 1);
  assert(sensor.isReady());
  const uint32_t recovery = 6000 + Config::SCD_MEASUREMENT_TIMEOUT_MS;
  sensor.poll(recovery);
  assert(!sensor.isReady());
  finishInitialization(sensor, recovery);
  Measurement replacement;
  sensor.read(replacement, recovery + 6000);
  assert(replacement.co2Valid && replacement.scdSerialNumber == 2);
  assert(replacement.scdTemperatureOffset == Config::SCD41_TEMPERATURE_OFFSET_C);
  assert(fakeScd.starts == 2);

  // A missing device must not prevent PT100 sampling; retries also find Wire1.
  fakeScd.present = false;
  uint32_t now = recovery + 11000;
  for (int i = 0; i < Config::SCD_MAX_CONSECUTIVE_ERRORS; ++i) {
    Measurement disconnected;
    sensor.read(disconnected, now);
    assert(!disconnected.co2Valid && disconnected.outerTemperatureValid);
    now += 5000;
  }
  assert(!sensor.isReady());
  fakeScd.present = true;
  fakeScd.bus = 1;
  fakeScd.serial = 3;
  fakeScd.measuring = false;
  const uint32_t started = now - 5000;
  finishInitialization(sensor, started);
  finishInitialization(sensor, started + Config::SCD_WAKE_DELAY_MS + Config::SCD_STOP_DELAY_MS);
  Measurement otherBus;
  sensor.read(otherBus, now + 6000);
  assert(otherBus.co2Valid && otherBus.scdSerialNumber == 3);
  assert(!MachineControl_RTDTempProbe.boxSelected);

  SensorManager combined;
  fakeSht = FakeSht{};
  fakeScd = FakeScdState{};
  combined.begin(0);
  finishInitialization(combined, 0);
  Measurement both;
  combined.read(both, 1000);
  assert(both.boxTemperatureValid && both.humidityValid);
  assert(both.boxTemperature > 24.9f && both.boxTemperature < 25.1f);
  assert(both.humidity > 93.9f && both.humidity < 94.1f);
  assert(both.scdHumidityValid && both.scdHumidity == 90);
  assert(both.outerTemperatureValid);
  fakeSht.present = false;
  combined.poll(5000);
  Measurement held;
  combined.read(held, 5000);
  assert(held.humidityValid && held.humidity == both.humidity);
  assert(held.shtReadUptimeMs == both.shtReadUptimeMs);
  assert(held.co2Valid && held.outerTemperatureValid);
  // At the next scheduled SHT read, a failure replaces the held value.
  combined.poll(66000);
  Measurement disconnectedSht;
  combined.read(disconnectedSht, 66000);
  assert(!disconnectedSht.humidityValid && disconnectedSht.outerTemperatureValid);

  fakeSht = FakeSht{};
  Sht45Sensor raw;
  raw.begin(0);
  raw.poll(10);
  float rawTemperature = NAN;
  float rawHumidity = NAN;
  fakeSht.corrupt = true;
  raw.poll(5000);
  raw.poll(5010);
  assert(!raw.read(rawTemperature, rawHumidity, 5010));
  fakeSht.corrupt = false;
  fakeSht.shortResponse = true;
  raw.poll(10000);
  raw.poll(10010);
  assert(!raw.read(rawTemperature, rawHumidity, 10010));
  fakeSht.shortResponse = false;
  fakeSht.bus = 1;
  fakeSht.humidity = 65535;
  raw.poll(15000);
  raw.poll(15010);
  assert(raw.read(rawTemperature, rawHumidity, 15010) && rawHumidity == 100);
  assert(!raw.read(rawTemperature, rawHumidity, 25010));
  // Readout requests a heater pulse; no heated/early cooldown sample is published.
  fakeSht = FakeSht{};
  Sht45Sensor heater;
  heater.begin(0);
  heater.poll(10);
  float temperature = NAN;
  float humidity = NAN;
  const uint32_t interval = Config::SHT45_HEATER_INTERVAL_MS;
  assert(heater.read(temperature, humidity, 1000));
  for (uint32_t time = 5000; time < interval; time += 5000) {
    heater.poll(time);
    heater.poll(time + 10);
  }
  assert(fakeSht.heaterCommands == 0);
  assert(heater.read(temperature, humidity, interval));
  assert(heater.heaterElapsedMs() == -1);
  heater.poll(interval);
  assert(fakeSht.heaterCommands == 1);
  fakeSht.temperature = 48000;
  heater.poll(interval + 1110);
  assert(!heater.read(temperature, humidity, interval + 1110));
  heater.poll(interval + 61109);
  assert(fakeSht.lastCommand == 0x39);
  fakeSht.temperature = 26214;
  heater.poll(interval + 61110);
  assert(fakeSht.lastCommand == 0xFD);
  heater.poll(interval + 61120);
  assert(heater.read(temperature, humidity, 2 * interval));
  assert(heater.heaterElapsedMs() == 60010);
  assert(!heater.heatedReading() && !heater.coolingReading());
  heater.poll(2 * interval);
  assert(fakeSht.heaterCommands == 2);
  // Continuous operation also beyond the old six-pulse test limit.
  for (uint32_t cycle = 2; cycle <= 8; ++cycle) {
    const uint32_t started = cycle * interval;
    heater.poll(started + 1110);
    heater.poll(started + 61110);
    heater.poll(started + 61120);
    assert(heater.read(temperature, humidity, started + interval));
    heater.poll(started + interval);
  }
  assert(fakeSht.heaterCommands == 9);
  // CRC failures remain invalid and cannot immediately reheat the sensor.
  fakeSht.corrupt = true;
  heater.poll(9 * interval + 1110);
  assert(!heater.read(temperature, humidity, 9 * interval + 1110));
  heater.poll(9 * interval + 5000);
  assert(fakeSht.heaterCommands == 9);
  // Exercise the real acquisition cadence with both sensors and correction metadata.
  fakeSht = FakeSht{};
  fakeScd = FakeScdState{};
  SensorManager regular;
  regular.begin(0);
  uint32_t previousReadAt = 0;
  float previousHumidity = NAN;
  uint16_t shtUpdates = 0;
  uint16_t fastUpdates = 0;
  for (uint32_t time = 10; time <= 10 * interval; time += 10) {
    regular.poll(time);
    if (time < 8000 || (time - 8000) % Config::MEASUREMENT_INTERVAL_MS != 0) continue;
    Measurement sample;
    regular.read(sample, time);
    assert(sample.humidityValid && sample.boxTemperatureValid && sample.co2Valid);
    assert(!sample.shtHeated && !sample.shtCooling);
    ++fastUpdates;
    if (sample.shtReadUptimeMs != previousReadAt) {
      if (previousReadAt) assert(sample.shtReadUptimeMs - previousReadAt == interval);
      previousReadAt = sample.shtReadUptimeMs;
      previousHumidity = sample.humidity;
      ++shtUpdates;
    } else {
      assert(sample.humidity == previousHumidity);
    }
    if (sample.shtReadUptimeMs <= interval + 8000) {
      assert(sample.humidityOffset == 0 && sample.shtHeaterElapsedMs == -1);
    } else {
      assert(sample.humidityOffset == 1.0f);
      assert(sample.shtHeaterElapsedMs >= 60000 && sample.shtHeaterElapsedMs < 65000);
    }
  }
  assert(shtUpdates == 10 && fastUpdates > 120);
  assert(!MachineControl_RTDTempProbe.boxSelected);
  return 0;
}
