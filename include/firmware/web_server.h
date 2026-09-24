#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "firmware/firmware_updater.h"
#include "firmware/measurement_controller.h"
#include "firmware/qspi_storage.h"

class WebServer {
 public:
  WebServer(MeasurementController& measurements, QspiStorage& storage,
            FirmwareUpdater& firmwareUpdater);

  void begin(uint32_t now);
  void poll(uint32_t now);

 private:
  enum class ClientState {
    idle,
    readingHeaders,
    readingMultipartHeaders,
    receivingUpload,
    sendingResponse,
    streamingBacklog,
  };

  void serviceWifi(uint32_t now);
  void acceptClient(uint32_t now);
  void processClient(uint32_t now);
  void readRequestBytes(uint32_t now);
  void readHeaderByte(char byte);
  void readMultipartHeaderByte(char byte);
  void readUploadBytes(size_t& byteBudget, uint32_t now);
  void handleRequest();
  void prepareUpload();
  void finishUpload();

  String buildApiJson(bool includeHistory) const;
  void startApiResponse();
  void prepareNextHistoryChunk();
  String headerValue(const char* name) const;
  String multipartBoundary(const String& contentType) const;
  bool parseContentLength(const String& value, size_t& result) const;
  bool requestMethodAndPath(String& method, String& path) const;

  void startResponse(uint16_t status, const char* contentType,
                     const String& body, bool restartAfterResponse = false);
  void sendResponseChunk(uint32_t now);
  void startBacklog(bool currentOnly = false);
  void sendBacklogChunk(uint32_t now);
  void sendResponseHeaders(uint16_t status, const char* contentType,
                           int contentLength);
  const char* reasonPhrase(uint16_t status) const;
  bool requestTimedOut(uint32_t now) const;
  bool isReadingRequest() const;
  void closeClient();

  MeasurementController& measurements_;
  QspiStorage& storage_;
  FirmwareUpdater& firmwareUpdater_;
  WiFiServer server_;
  WiFiClient client_;
  BacklogReader backlogReader_;
  ClientState clientState_ = ClientState::idle;
  String requestHeaders_;
  String multipartHeaders_;
  String boundary_;
  String responseBody_;
  size_t responseOffset_ = 0;
  Measurement apiHistory_[Config::HISTORY_SIZE];
  size_t apiHistoryCount_ = 0;
  size_t apiHistoryIndex_ = 0;
  bool streamingApiHistory_ = false;
  size_t expectedBodyBytes_ = 0;
  size_t receivedBodyBytes_ = 0;
  uint8_t streamBuffer_[Config::HTTP_STREAM_BUFFER_BYTES] = {};
  size_t streamBufferSize_ = 0;
  size_t streamBufferOffset_ = 0;
  uint32_t lastWifiAttemptAt_ = 0;
  uint32_t lastClientActivityAt_ = 0;
  bool serverStarted_ = false;
  bool restartAfterResponse_ = false;
};
