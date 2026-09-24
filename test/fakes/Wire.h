#pragma once
#include <stdint.h>
struct FakeSht {
  bool present = true;
  int bus = 0;
  uint8_t lastCommand = 0;
  int heaterCommands = 0;
  bool corrupt = false;
  bool shortResponse = false;
  uint16_t temperature = 26214;
  uint16_t humidity = 52428;
};
inline FakeSht fakeSht;
struct TwoWire {
  int index;
  int cursor = 0;
  void begin() {}
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t) {}
  uint8_t command = 0;
  void write(uint8_t value) { command = value; }
  int endTransmission() {
    if (!fakeSht.present || fakeSht.bus != index) return 1;
    fakeSht.lastCommand = command;
    if (command == 0x39) ++fakeSht.heaterCommands;
    return 0;
  }
  uint8_t requestFrom(uint8_t, uint8_t) {
    cursor = 0;
    return fakeSht.present && !fakeSht.shortResponse ? 6 : 0;
  }
  int read() {
    const int position = cursor++;
    const uint16_t word = position < 3 ? fakeSht.temperature : fakeSht.humidity;
    if (position % 3 == 0) return word >> 8;
    if (position % 3 == 1) return word & 0xFF;
    uint8_t crc = 0xFF;
    for (int i = 0; i < 2; ++i) {
      crc ^= i == 0 ? word >> 8 : word & 0xFF;
      for (int bit = 0; bit < 8; ++bit) {
        crc = crc & 0x80 ? (crc << 1) ^ 0x31 : crc << 1;
      }
    }
    return fakeSht.corrupt ? crc ^ 1 : crc;
  }
};
inline TwoWire Wire{0};
inline TwoWire Wire1{1};
