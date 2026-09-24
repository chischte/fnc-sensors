#pragma once
#include <stdint.h>
constexpr int THREE_WIRE = 1;
struct FakeRtd {
  void begin(int) {}
  uint8_t selectedChannels = 0;
  void selectChannel(uint8_t channel) { selectedChannels |= 1U << channel; }
  float readTemperature(float, float) { return 25.0f; }
  uint8_t readFault() { return 0; }
  void clearFault() {}
};
inline FakeRtd MachineControl_RTDTempProbe;
