#pragma once
#include <stdint.h>
constexpr int THREE_WIRE = 1;
struct FakeRtd {
  void begin(int) {}
  bool boxSelected = false;
  void selectChannel(uint8_t channel) { if (channel == 1) boxSelected = true; }
  float readTemperature(float, float) { return 25.0f; }
  uint8_t readFault() { return 0; }
  void clearFault() {}
};
inline FakeRtd MachineControl_RTDTempProbe;
