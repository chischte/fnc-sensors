#include "firmware/qspi_storage.h"

#include <BlockDevice.h>
#include <FATFileSystem.h>
#include <MBRBlockDevice.h>
#include <sys/stat.h>
#include <hal/trng_api.h>
#include <objects.h>

#include "config.h"
#include "firmware/measurement_json.h"

namespace {
constexpr uint8_t OTA_PARTITION = 2;
constexpr uint8_t USER_DATA_PARTITION = 4;
constexpr uint32_t BOOT_ID_FALLBACK_MASK = 0xA5C3417D;
constexpr char LOG_PATH[] = "/data/MEASUREMENTS.NDJSON";
constexpr char OLD_LOG_PATH[] = "/data/MEASUREMENTS.OLD";
constexpr char BOOT_ID_PATH[] = "/data/BOOT.ID";
constexpr const char* BACKLOG_PATHS[] = {OLD_LOG_PATH, LOG_PATH};
constexpr size_t BACKLOG_FILE_COUNT =
    sizeof(BACKLOG_PATHS) / sizeof(BACKLOG_PATHS[0]);
}  // namespace

bool BacklogReader::begin(bool storageReady, bool currentOnly) {
  close();
  nextFileIndex_ = currentOnly ? BACKLOG_FILE_COUNT - 1 : 0;
  finished_ = !storageReady;
  return storageReady && openNextFile();
}

size_t BacklogReader::read(uint8_t* buffer, size_t capacity) {
  while (file_) {
    const size_t bytesRead = fread(buffer, 1, capacity, file_);
    if (bytesRead) {
      return bytesRead;
    }
    fclose(file_);
    file_ = nullptr;
    openNextFile();
  }
  return 0;
}

bool BacklogReader::isFinished() const {
  return finished_;
}

void BacklogReader::close() {
  if (file_) {
    fclose(file_);
    file_ = nullptr;
  }
  finished_ = true;
}

bool BacklogReader::openNextFile() {
  while (nextFileIndex_ < BACKLOG_FILE_COUNT) {
    file_ = fopen(BACKLOG_PATHS[nextFileIndex_], "r");
    ++nextFileIndex_;
    if (file_) {
      finished_ = false;
      return true;
    }
  }
  finished_ = true;
  return false;
}

void QspiStorage::begin() {
  rootDevice_ = mbed::BlockDevice::get_default_instance();
  if (!rootDevice_) {
    Serial.println("QSPI block device missing");
    return;
  }

  const int initializationResult = rootDevice_->init();
  if (initializationResult) {
    Serial.print("QSPI already initialized/status: ");
    Serial.println(initializationResult);
  }

  dataReady_ = mountPartition(USER_DATA_PARTITION, "data", "QSPI",
                              dataDevice_, dataFileSystem_);
  otaReady_ = mountPartition(OTA_PARTITION, "fs", "OTA", otaDevice_,
                             otaFileSystem_);
}

uint32_t QspiStorage::nextBootId() {
  uint32_t previousBootId = 0;
  if (dataReady_) {
    FILE* file = fopen(BOOT_ID_PATH, "rb");
    if (file) {
      if (fread(&previousBootId, sizeof(previousBootId), 1, file) != 1) {
        previousBootId = 0;
      }
      fclose(file);
    }
  }

  // A restored/reset flash counter can reuse IDs already present in the logger.
  trng_t randomSource;
  uint32_t bootId = 0;
  size_t randomBytes = 0;
  trng_init(&randomSource);
  const int randomResult = trng_get_bytes(
      &randomSource, reinterpret_cast<uint8_t*>(&bootId), sizeof(bootId),
      &randomBytes);
  trng_free(&randomSource);
  if (randomResult || randomBytes != sizeof(bootId) || !bootId ||
      bootId == previousBootId) {
    Serial.println("Boot ID entropy unavailable; using persisted fallback");
    bootId = previousBootId ? previousBootId + 1 :
        (static_cast<uint32_t>(micros()) ^ BOOT_ID_FALLBACK_MASK);
    if (!bootId) {
      bootId = BOOT_ID_FALLBACK_MASK;
    }
  }

  if (dataReady_) {
    FILE* file = fopen(BOOT_ID_PATH, "wb");
    if (file) {
      if (fwrite(&bootId, sizeof(bootId), 1, file) != 1) {
        Serial.println("Could not persist boot ID");
      }
      fflush(file);
      fclose(file);
    } else {
      Serial.println("Could not open boot ID file");
    }
  }
  return bootId;
}

void QspiStorage::append(const Measurement& measurement, uint32_t bootId) {
  if (!dataReady_) {
    return;
  }

  rotateLogIfNeeded();
  FILE* file = fopen(LOG_PATH, "a");
  if (!file) {
    dataReady_ = false;
    return;
  }

  String line;
  line.reserve(220);
  appendMeasurementJson(line, measurement, bootId);
  line += '\n';
  if (fwrite(line.c_str(), 1, line.length(), file) != line.length()) {
    dataReady_ = false;
  }
  fflush(file);
  fclose(file);
}

bool QspiStorage::isDataReady() const {
  return dataReady_;
}

bool QspiStorage::isOtaReady() const {
  return otaReady_;
}

bool QspiStorage::mountPartition(
    uint8_t partition, const char* mountName, const char* description,
    mbed::MBRBlockDevice*& partitionDevice,
    mbed::FATFileSystem*& fileSystem) {
  partitionDevice = new mbed::MBRBlockDevice(rootDevice_, partition);
  fileSystem = new mbed::FATFileSystem(mountName);
  int mountResult = fileSystem->mount(partitionDevice);
  if (mountResult) {
    Serial.print(description);
    Serial.print(" mount failed, formatting partition ");
    Serial.print(partition);
    Serial.print(": ");
    Serial.println(mountResult);
    mountResult = fileSystem->reformat(partitionDevice);
  }

  const bool ready = mountResult == 0;
  Serial.print(description);
  Serial.println(ready ? " ready" : " unavailable");
  return ready;
}

void QspiStorage::rotateLogIfNeeded() {
  struct stat status {};
  if (stat(LOG_PATH, &status) ||
      static_cast<size_t>(status.st_size) <
          Config::PERSISTENT_LOG_MAX_BYTES) {
    return;
  }
  remove(OLD_LOG_PATH);
  rename(LOG_PATH, OLD_LOG_PATH);
}
