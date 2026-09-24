#pragma once

#include <Arduino.h>
#include <cstdio>

#include "firmware/measurement.h"

namespace mbed {
class BlockDevice;
class FATFileSystem;
class MBRBlockDevice;
}  // namespace mbed

class BacklogReader {
 public:
  bool begin(bool storageReady, bool currentOnly = false);
  size_t read(uint8_t* buffer, size_t capacity);
  bool isFinished() const;
  void close();

 private:
  bool openNextFile();

  FILE* file_ = nullptr;
  uint8_t nextFileIndex_ = 0;
  bool finished_ = true;
};

class QspiStorage {
 public:
  void begin();
  uint32_t nextBootId();
  void append(const Measurement& measurement, uint32_t bootId);

  bool isDataReady() const;
  bool isOtaReady() const;

 private:
  bool mountPartition(uint8_t partition, const char* mountName,
                      const char* description,
                      mbed::MBRBlockDevice*& partitionDevice,
                      mbed::FATFileSystem*& fileSystem);
  void rotateLogIfNeeded();

  mbed::BlockDevice* rootDevice_ = nullptr;
  mbed::MBRBlockDevice* dataDevice_ = nullptr;
  mbed::MBRBlockDevice* otaDevice_ = nullptr;
  mbed::FATFileSystem* dataFileSystem_ = nullptr;
  mbed::FATFileSystem* otaFileSystem_ = nullptr;
  bool dataReady_ = false;
  bool otaReady_ = false;
};
