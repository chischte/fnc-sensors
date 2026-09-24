#pragma once

#include <Arduino.h>
#include <Wire.h>

class Sht45Sensor {
 public:
  void begin(uint32_t now);
  void poll(uint32_t now);
  bool read(float& temperature, float& humidity, uint32_t now);
  bool heatedReading() const;
  bool coolingReading() const;
  int32_t heaterElapsedMs() const;

 private:
  bool startMeasurement(TwoWire& bus, uint32_t now);
  void finishMeasurement(uint32_t now);
  bool heaterDue(uint32_t now) const;

  TwoWire* bus_ = nullptr;
  uint32_t lastAttemptAt_ = 0;
  uint32_t lastReadingAt_ = 0;
  uint32_t lastHeaterStartedAt_ = 0;
  bool heaterRequested_ = false;
  float temperature_ = NAN;
  float humidity_ = NAN;
  bool pending_ = false;
  bool valid_ = false;
  bool heating_ = false;
  bool hasHeated_ = false;
  bool heatedReading_ = false;
};
