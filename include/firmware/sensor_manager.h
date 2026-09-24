#pragma once

#include <Arduino.h>
#include <SensirionI2cScd4x.h>
#include <Wire.h>

#include "firmware/measurement.h"
#include "firmware/sht45_sensor.h"

class SensorManager {
 public:
  void begin(uint32_t now);
  void poll(uint32_t now);
  void read(Measurement& measurement, uint32_t now);

  bool isReady() const;
  uint16_t errorCount() const;

 private:
  enum class InitializationState {
    idle,
    waitingAfterWake,
    waitingAfterStop,
  };

  void startInitialization(uint8_t busIndex, uint32_t now);
  void continueInitialization(uint32_t now);
  void finishInitialization(uint32_t now);
  void tryNextBus(uint32_t now);
  void readSht(Measurement& measurement, uint32_t now);
  void readScd(Measurement& measurement, uint32_t now);
  void readRtd(uint8_t channel, float& value, bool& valid, uint8_t& fault);
  void compareRtdModes(Measurement& measurement);
  void recordScdError(const char* operation, int16_t error, uint32_t now);
  TwoWire& busForIndex(uint8_t busIndex);
  const char* busName(uint8_t busIndex) const;

  struct ShtReading {
    float temperature = NAN;
    float humidity = NAN;
    float offset = 0.0f;
    int32_t heaterElapsedMs = -1;
    uint32_t readAt = 0;
    bool valid = false;
  };
  ShtReading shtReading_;
  Sht45Sensor sht45_;
  SensirionI2cScd4x scd4x_;
  InitializationState initializationState_ = InitializationState::idle;
  uint32_t stateStartedAt_ = 0;
  uint32_t lastInitializationAt_ = 0;
  uint32_t lastScdMeasurementAt_ = 0;
  uint64_t scdSerialNumber_ = 0;
  uint16_t errorCount_ = 0;
  uint8_t busIndex_ = 0;
  bool ready_ = false;
  float scdTemperatureOffset_ = NAN;
};
