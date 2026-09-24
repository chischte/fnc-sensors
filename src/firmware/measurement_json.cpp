#include "firmware/measurement_json.h"

namespace {
String nullableNumber(float value, bool valid) {
  return valid && isfinite(value) ? String(value, 2) : String("null");
}

const char* booleanJson(bool value) {
  return value ? "true" : "false";
}
void appendHumidityJson(String& json, const Measurement& measurement) {
  json += ",\"sht_read_uptime_ms\":" + String(measurement.shtReadUptimeMs);
  json += ",\"humidity\":" +
          nullableNumber(measurement.humidity, measurement.humidityValid);
  json += ",\"humidity_offset_rh\":" + String(measurement.humidityOffset, 2);
  json += ",\"humidity_corrected\":" + nullableNumber(
      min(100.0f, measurement.humidity + measurement.humidityOffset),
      measurement.humidityValid);
  json += ",\"sht_heater_elapsed_ms\":";
  json += measurement.shtHeaterElapsedMs >= 0
      ? String(measurement.shtHeaterElapsedMs) : String("null");
  json += ",\"sht_heated\":" + String(booleanJson(measurement.shtHeated));
  json += ",\"sht_cooling\":" + String(booleanJson(measurement.shtCooling));
}
}  // namespace

void appendMeasurementJson(String& json, const Measurement& measurement,
                           uint32_t bootId) {
  json += "{\"boot_id\":" + String(bootId);
  json += ",\"sequence\":" + String(measurement.sequence);
  json += ",\"uptime_ms\":" + String(measurement.uptimeMs);
  json += ",\"co2\":";
  json += measurement.co2Valid ? String(measurement.co2) : String("null");
  json += ",\"boxtemp\":" +
          nullableNumber(measurement.boxTemperature,
                         measurement.boxTemperatureValid);
  appendHumidityJson(json, measurement);
  json += ",\"scd_humidity\":" +
          nullableNumber(measurement.scdHumidity, measurement.scdHumidityValid);
  json += ",\"outertemp\":" +
          nullableNumber(measurement.outerTemperature,
                         measurement.outerTemperatureValid);
  json += ",\"scdtemp\":" + nullableNumber(measurement.scdTemperature,
                                              measurement.co2Valid);
  json += ",\"scd_offset\":" + nullableNumber(measurement.scdTemperatureOffset,
                                                 measurement.co2Valid);
  json += ",\"scd_serial\":";
  if (measurement.co2Valid) {
    char serial[13];
    snprintf(serial, sizeof(serial), "%04lX%08lX",
             static_cast<unsigned long>(measurement.scdSerialNumber >> 32),
             static_cast<unsigned long>(measurement.scdSerialNumber & 0xFFFFFFFF));
    json += "\"" + String(serial) + "\"";
  } else {
    json += "null";
  }
  json += ",\"rtd_outer_raw\":" + String(measurement.outerRaw);
  json += ",\"rtd_outer_config_before\":" + String(measurement.outerDiagnostics.configBefore);
  json += ",\"rtd_outer_config_after\":" + String(measurement.outerDiagnostics.configAfter);
  json += ",\"rtd_config_recoveries\":" + String(measurement.outerDiagnostics.recoveries);
  if (measurement.rtdComparison) {
    const String suffix = measurement.rtdComparisonTwoWire ? "_2wire" : "_60hz";
    json += ",\"rtd_comparison\":{\"outer" + suffix + "\":" +
        nullableNumber(measurement.outerComparisonTemperature, !measurement.outerComparisonFault);
    json += ",\"outer_raw" + suffix + "\":" + String(measurement.outerComparisonRaw);
    json += ",\"outer_fault" + suffix + "\":" + String(measurement.outerComparisonFault) + "}";
  }
  json += ",\"valid\":{\"co2\":";
  json += booleanJson(measurement.co2Valid);
  json += ",\"boxtemp\":";
  json += booleanJson(measurement.boxTemperatureValid);
  json += ",\"humidity\":";
  json += booleanJson(measurement.humidityValid);
  json += ",\"scd_humidity\":";
  json += booleanJson(measurement.scdHumidityValid);
  json += ",\"outertemp\":";
  json += booleanJson(measurement.outerTemperatureValid);
  json += "},\"faults\":{\"rtd_outer\":" + String(measurement.outerFault) + "}}";
}

void appendHistoryMeasurementJson(String& json,
                                  const Measurement& measurement) {
  json += "{\"uptime_ms\":" + String(measurement.uptimeMs);
  json += ",\"co2\":";
  json += measurement.co2Valid ? String(measurement.co2) : String("null");
  json += ",\"boxtemp\":" +
          nullableNumber(measurement.boxTemperature,
                         measurement.boxTemperatureValid);
  appendHumidityJson(json, measurement);
  json += ",\"scd_humidity\":" +
          nullableNumber(measurement.scdHumidity, measurement.scdHumidityValid);
  json += ",\"outertemp\":" +
          nullableNumber(measurement.outerTemperature,
                         measurement.outerTemperatureValid);
  json += '}';
}
