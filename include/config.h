#pragma once

#include <Arduino.h>

namespace Config {

// Hardware mapping
constexpr uint8_t SHT45_I2C_ADDRESS = 0x44;
constexpr uint8_t RTD_OUTER_CHANNEL = 0;
constexpr float RTD_REFERENCE_OHMS = 400.0f;
constexpr float RTD_NOMINAL_OHMS = 100.0f;
constexpr float RTD_MIN_TEMPERATURE_C = -100.0f;
constexpr float RTD_MAX_TEMPERATURE_C = 200.0f;
constexpr float HUMIDITY_MIN_RH = 0.0f;
constexpr float HUMIDITY_MAX_RH = 100.0f;
constexpr uint32_t I2C_CLOCK_HZ = 100000;

// Acquisition and recovery
constexpr uint32_t MEASUREMENT_INTERVAL_MS = 5000;
constexpr uint32_t SHT45_SAMPLE_INTERVAL_MS = 5000;
constexpr uint32_t SHT45_READ_INTERVAL_MS = 65000;
constexpr float SHT45_HUMIDITY_OFFSET_RH = 1.0f;
// Heat after acquisition; publish the next reading after at least 60 s cooldown.
constexpr bool SHT45_HEATER_ENABLED = true;
constexpr uint32_t SHT45_HEATER_INTERVAL_MS = SHT45_READ_INTERVAL_MS;
constexpr uint32_t SHT45_HEATER_COOLDOWN_MS = 60000;
constexpr float SHT45_HEATER_MAX_AMBIENT_C = 65.0f;
constexpr uint32_t SENSOR_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t SCD_MEASUREMENT_TIMEOUT_MS = 3 * MEASUREMENT_INTERVAL_MS;
constexpr uint32_t SCD_WAKE_DELAY_MS = 35;
constexpr uint32_t SCD_STOP_DELAY_MS = 500;
constexpr uint16_t SCD_MAX_CONSECUTIVE_ERRORS = 3;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
constexpr size_t HISTORY_SIZE = 360;

// HTTP work is deliberately bounded so the main loop can keep sampling sensors.
constexpr uint16_t HTTP_PORT = 80;
constexpr uint32_t HTTP_REQUEST_TIMEOUT_MS = 5000;
constexpr size_t HTTP_MAX_HEADER_BYTES = 4096;
constexpr size_t HTTP_MAX_MULTIPART_HEADER_BYTES = 2048;
constexpr size_t HTTP_BYTES_PER_POLL = 2048;
constexpr size_t HTTP_STREAM_BUFFER_BYTES = 512;

// SCD41 installation settings. Adjust after calibration in the final enclosure.
// Historical offset matched to the former inner PT100 on 2026-09-08.
constexpr float SCD41_TEMPERATURE_OFFSET_C = 2.2f;
// Optional paired converter comparison after startup. Zero disables it.
constexpr uint32_t RTD_DIAGNOSTIC_FIRST_SEQUENCE = 7;
constexpr uint32_t RTD_DIAGNOSTIC_SAMPLES = 0;
constexpr bool RTD_DIAGNOSTIC_TWO_WIRE = false;
constexpr uint16_t SCD41_ALTITUDE_M = 450; // Site altitude; pressure input would override this.
// Closed chambers normally do not see fresh 400 ppm air regularly, so ASC is disabled.
constexpr bool SCD41_ASC_ENABLED = false;

// QSPI append-only recovery buffer. The older file is retained across one rotation.
constexpr size_t PERSISTENT_LOG_MAX_BYTES = 4U * 1024U * 1024U;
constexpr size_t OTA_MAX_FILE_BYTES = 5U * 1024U * 1024U;
constexpr const char* FIRMWARE_VERSION = "2.0.13";

}  // namespace Config
