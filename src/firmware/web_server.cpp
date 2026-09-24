#include "firmware/web_server.h"

#include "firmware/measurement_json.h"
#include "firmware/time_utils.h"
#include "wifi-credentials.h"
#include "web_ui.h"

namespace {
constexpr int UNKNOWN_CONTENT_LENGTH = -1;
constexpr size_t RESPONSE_CHUNK_BYTES = 1024;
constexpr char HEADER_TERMINATOR[] = "\r\n\r\n";
}  // namespace

WebServer::WebServer(MeasurementController& measurements,
                     QspiStorage& storage,
                     FirmwareUpdater& firmwareUpdater)
    : measurements_(measurements),
      storage_(storage),
      firmwareUpdater_(firmwareUpdater),
      server_(Config::HTTP_PORT) {}

void WebServer::begin(uint32_t now) {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWifiAttemptAt_ = now;
}

void WebServer::poll(uint32_t now) {
  serviceWifi(now);
  if (!serverStarted_) {
    return;
  }
  if (clientState_ == ClientState::idle) {
    acceptClient(now);
  }
  if (clientState_ != ClientState::idle) {
    processClient(now);
  }
}

void WebServer::serviceWifi(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!serverStarted_) {
      server_.begin();
      serverStarted_ = true;
      Serial.println(WiFi.localIP());
    }
    return;
  }

  if (clientState_ != ClientState::idle) {
    closeClient();
  }
  serverStarted_ = false;
  if (hasElapsed(now, lastWifiAttemptAt_,
                 Config::WIFI_RECONNECT_INTERVAL_MS)) {
    lastWifiAttemptAt_ = now;
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void WebServer::acceptClient(uint32_t now) {
  WiFiClient incomingClient = server_.accept();
  if (!incomingClient) {
    return;
  }

  client_ = incomingClient;
  clientState_ = ClientState::readingHeaders;
  requestHeaders_ = "";
  requestHeaders_.reserve(1024);
  lastClientActivityAt_ = now;
}

void WebServer::processClient(uint32_t now) {
  if (!client_.connected() && !client_.available()) {
    closeClient();
    return;
  }
  if (requestTimedOut(now)) {
    firmwareUpdater_.cancelUpload();
    startResponse(408, "text/plain", "Request timed out.");
  }

  if (clientState_ == ClientState::sendingResponse) {
    sendResponseChunk(now);
    return;
  }
  if (clientState_ == ClientState::streamingBacklog) {
    sendBacklogChunk(now);
    return;
  }
  readRequestBytes(now);
}

void WebServer::readRequestBytes(uint32_t now) {
  size_t byteBudget = Config::HTTP_BYTES_PER_POLL;
  while (byteBudget && client_.available() &&
         isReadingRequest()) {
    if (clientState_ == ClientState::receivingUpload) {
      readUploadBytes(byteBudget, now);
      continue;
    }

    const int value = client_.read();
    if (value < 0) {
      break;
    }
    --byteBudget;
    lastClientActivityAt_ = now;
    if (clientState_ == ClientState::readingHeaders) {
      readHeaderByte(static_cast<char>(value));
    } else {
      ++receivedBodyBytes_;
      readMultipartHeaderByte(static_cast<char>(value));
    }
  }
  if (clientState_ == ClientState::receivingUpload &&
      receivedBodyBytes_ == expectedBodyBytes_) {
    finishUpload();
  }
}

void WebServer::readHeaderByte(char byte) {
  requestHeaders_ += byte;
  if (requestHeaders_.length() > Config::HTTP_MAX_HEADER_BYTES) {
    startResponse(431, "text/plain", "Request headers are too large.");
    return;
  }
  if (requestHeaders_.endsWith(HEADER_TERMINATOR)) {
    handleRequest();
  }
}

void WebServer::readMultipartHeaderByte(char byte) {
  multipartHeaders_ += byte;
  if (receivedBodyBytes_ >= expectedBodyBytes_ &&
      !multipartHeaders_.endsWith(HEADER_TERMINATOR)) {
    startResponse(400, "text/plain", "Invalid multipart upload.");
    return;
  }
  if (multipartHeaders_.length() >
      Config::HTTP_MAX_MULTIPART_HEADER_BYTES) {
    startResponse(400, "text/plain", "Invalid multipart upload.");
    return;
  }
  if (!multipartHeaders_.endsWith(HEADER_TERMINATOR)) {
    return;
  }

  const String expectedStart = boundary_ + "\r\n";
  if (!multipartHeaders_.startsWith(expectedStart) ||
      multipartHeaders_.indexOf("Content-Disposition:") < 0) {
    startResponse(400, "text/plain", "Invalid multipart upload.");
    return;
  }
  if (!firmwareUpdater_.beginUpload()) {
    startResponse(500, "text/plain", "Cannot open OTA file.");
    return;
  }
  clientState_ = ClientState::receivingUpload;
}

void WebServer::readUploadBytes(size_t& byteBudget, uint32_t now) {
  const size_t remainingBodyBytes = expectedBodyBytes_ - receivedBodyBytes_;
  const size_t requestedBytes =
      min(min(static_cast<size_t>(client_.available()), byteBudget),
          min(remainingBodyBytes, sizeof(streamBuffer_)));
  if (!requestedBytes) {
    finishUpload();
    return;
  }

  const int bytesRead = client_.read(streamBuffer_, requestedBytes);
  if (bytesRead <= 0) {
    return;
  }
  const size_t bytesReadCount = static_cast<size_t>(bytesRead);
  byteBudget -= bytesReadCount;
  receivedBodyBytes_ += bytesReadCount;
  lastClientActivityAt_ = now;
  if (!firmwareUpdater_.write(streamBuffer_, bytesReadCount)) {
    firmwareUpdater_.cancelUpload();
    startResponse(400, "text/plain", "Invalid or unwritable OTA payload.");
    return;
  }
  if (receivedBodyBytes_ == expectedBodyBytes_) {
    finishUpload();
  }
}

void WebServer::handleRequest() {
  String method;
  String path;
  if (!requestMethodAndPath(method, path)) {
    startResponse(400, "text/plain", "Malformed request.");
    return;
  }

  if (method == "GET" && path == "/api/measurement") {
    startApiResponse();
  } else if (method == "GET" && path == "/api/current") {
    startResponse(200, "application/json", buildApiJson(false));
  } else if (method == "GET" && path == "/api/backlog/current") {
    startBacklog(true);
  } else if (method == "GET" && path == "/api/backlog") {
    startBacklog();
  } else if (method == "GET" && path == "/update") {
    startResponse(200, "text/html; charset=utf-8", UPDATE_HTML);
  } else if (method == "POST" && path == "/update") {
    prepareUpload();
  } else if (method == "GET" && (path == "/" || path == "/index.html")) {
    startResponse(200, "text/html; charset=utf-8", INDEX_HTML);
  } else {
    startResponse(404, "text/plain", "Not found.");
  }
}

void WebServer::prepareUpload() {
  const String contentLengthHeader = headerValue("Content-Length");
  const String contentType = headerValue("Content-Type");
  const size_t maximumRequestBytes =
      Config::OTA_MAX_FILE_BYTES + Config::HTTP_MAX_MULTIPART_HEADER_BYTES;
  if (!parseContentLength(contentLengthHeader, expectedBodyBytes_) ||
      expectedBodyBytes_ > maximumRequestBytes) {
    startResponse(400, "text/plain", "Invalid OTA content length.");
    return;
  }

  boundary_ = multipartBoundary(contentType);
  if (boundary_.length() < 3) {
    startResponse(400, "text/plain", "Invalid OTA boundary.");
    return;
  }

  receivedBodyBytes_ = 0;
  multipartHeaders_ = "";
  multipartHeaders_.reserve(256);
  clientState_ = ClientState::readingMultipartHeaders;
}

void WebServer::finishUpload() {
  if (receivedBodyBytes_ != expectedBodyBytes_) {
    firmwareUpdater_.cancelUpload();
    startResponse(400, "text/plain", "Incomplete OTA upload.");
    return;
  }
  const FirmwareUpdateResult result = firmwareUpdater_.finishAndInstall();
  startResponse(result.httpStatus, "text/plain", result.message,
                result.restartRequired);
}

String WebServer::buildApiJson(bool includeHistory) const {
  String json;
  json.reserve(2048);
  json += "{\"firmware\":\"";
  json += Config::FIRMWARE_VERSION;
  json += "\",\"sensor_ready\":";
  json += measurements_.sensorReady() ? "true" : "false";
  json += ",\"storage_ready\":";
  json += storage_.isDataReady() ? "true" : "false";
  json += ",\"scd_errors\":" + String(measurements_.sensorErrorCount());
  json += ",\"measurement\":";
  appendMeasurementJson(json, measurements_.current(), measurements_.bootId());
  json += includeHistory ? ",\"history\":[" : "}";
  return json;
}

void WebServer::startApiResponse() {
  // A bounded snapshot stays consistent while new sensor samples arrive.
  apiHistoryCount_ = measurements_.historyCount();
  for (size_t index = 0; index < apiHistoryCount_; ++index) {
    apiHistory_[index] = measurements_.historyAt(index);
  }
  apiHistoryIndex_ = 0;
  streamingApiHistory_ = true;
  responseBody_ = buildApiJson(true);
  responseOffset_ = 0;
  sendResponseHeaders(200, "application/json", UNKNOWN_CONTENT_LENGTH);
  clientState_ = ClientState::sendingResponse;
}

void WebServer::prepareNextHistoryChunk() {
  responseBody_ = "";
  responseOffset_ = 0;
  if (apiHistoryIndex_ == apiHistoryCount_) {
    responseBody_ = "]}";
    streamingApiHistory_ = false;
    return;
  }
  if (apiHistoryIndex_) responseBody_ += ',';
  appendHistoryMeasurementJson(responseBody_, apiHistory_[apiHistoryIndex_++]);
}

String WebServer::headerValue(const char* name) const {
  int lineStart = requestHeaders_.indexOf('\n') + 1;
  while (lineStart > 0 && lineStart < static_cast<int>(requestHeaders_.length())) {
    int lineEnd = requestHeaders_.indexOf('\n', lineStart);
    if (lineEnd < 0) {
      lineEnd = requestHeaders_.length();
    }
    const int separator = requestHeaders_.indexOf(':', lineStart);
    if (separator > lineStart && separator < lineEnd) {
      String key = requestHeaders_.substring(lineStart, separator);
      key.trim();
      if (key.equalsIgnoreCase(name)) {
        String value = requestHeaders_.substring(separator + 1, lineEnd);
        value.trim();
        return value;
      }
    }
    lineStart = lineEnd + 1;
  }
  return "";
}

String WebServer::multipartBoundary(const String& contentType) const {
  String lowerContentType = contentType;
  lowerContentType.toLowerCase();
  const int boundaryStart = lowerContentType.indexOf("boundary=");
  if (boundaryStart < 0) {
    return "";
  }

  String boundary = contentType.substring(boundaryStart + 9);
  const int optionSeparator = boundary.indexOf(';');
  if (optionSeparator >= 0) {
    boundary = boundary.substring(0, optionSeparator);
  }
  boundary.trim();
  if (boundary.startsWith("\"") && boundary.endsWith("\"") &&
      boundary.length() >= 2) {
    boundary = boundary.substring(1, boundary.length() - 1);
  }
  return "--" + boundary;
}

bool WebServer::parseContentLength(const String& value, size_t& result) const {
  if (!value.length()) {
    return false;
  }
  result = 0;
  for (size_t index = 0; index < value.length(); ++index) {
    const char digit = value[index];
    if (digit < '0' || digit > '9') {
      return false;
    }
    const size_t digitValue = static_cast<size_t>(digit - '0');
    const size_t maximumSize = static_cast<size_t>(-1);
    if (result > (maximumSize - digitValue) / 10) {
      return false;
    }
    result = result * 10 + digitValue;
  }
  return result > 0;
}

bool WebServer::requestMethodAndPath(String& method, String& path) const {
  const int lineEnd = requestHeaders_.indexOf('\n');
  const int firstSpace = requestHeaders_.indexOf(' ');
  const int secondSpace = requestHeaders_.indexOf(' ', firstSpace + 1);
  if (lineEnd < 0 || firstSpace <= 0 || secondSpace <= firstSpace ||
      secondSpace > lineEnd) {
    return false;
  }
  method = requestHeaders_.substring(0, firstSpace);
  path = requestHeaders_.substring(firstSpace + 1, secondSpace);
  return true;
}

void WebServer::startResponse(uint16_t status, const char* contentType,
                              const String& body,
                              bool restartAfterResponse) {
  sendResponseHeaders(status, contentType, body.length());
  responseBody_ = body;
  responseOffset_ = 0;
  restartAfterResponse_ = restartAfterResponse;
  clientState_ = ClientState::sendingResponse;
}

void WebServer::sendResponseChunk(uint32_t now) {
  const size_t remainingBytes = responseBody_.length() - responseOffset_;
  const size_t requestedBytes = min(remainingBytes, RESPONSE_CHUNK_BYTES);
  if (requestedBytes) {
    const uint8_t* data = reinterpret_cast<const uint8_t*>(
        responseBody_.c_str() + responseOffset_);
    responseOffset_ += client_.write(data, requestedBytes);
    lastClientActivityAt_ = now;
    return;
  }

  if (streamingApiHistory_) {
    prepareNextHistoryChunk();
    return;
  }

  const bool restart = restartAfterResponse_;
  closeClient();
  if (restart) {
    firmwareUpdater_.restart();
  }
}

void WebServer::startBacklog(bool currentOnly) {
  sendResponseHeaders(200, "application/x-ndjson", UNKNOWN_CONTENT_LENGTH);
  backlogReader_.begin(storage_.isDataReady(), currentOnly);
  streamBufferSize_ = 0;
  streamBufferOffset_ = 0;
  clientState_ = ClientState::streamingBacklog;
}

void WebServer::sendBacklogChunk(uint32_t now) {
  if (streamBufferOffset_ == streamBufferSize_) {
    streamBufferSize_ =
        backlogReader_.read(streamBuffer_, sizeof(streamBuffer_));
    streamBufferOffset_ = 0;
  }
  if (!streamBufferSize_ && backlogReader_.isFinished()) {
    closeClient();
    return;
  }

  const size_t bytesWritten = client_.write(
      streamBuffer_ + streamBufferOffset_,
      streamBufferSize_ - streamBufferOffset_);
  streamBufferOffset_ += bytesWritten;
  lastClientActivityAt_ = now;
}

void WebServer::sendResponseHeaders(uint16_t status, const char* contentType,
                                    int contentLength) {
  client_.print("HTTP/1.1 ");
  client_.print(status);
  client_.print(' ');
  client_.println(reasonPhrase(status));
  client_.print("Content-Type: ");
  client_.println(contentType);
  if (contentLength >= 0) {
    client_.print("Content-Length: ");
    client_.println(contentLength);
  }
  client_.println("Cache-Control: no-store");
  client_.println("Connection: close\r\n");
}

const char* WebServer::reasonPhrase(uint16_t status) const {
  switch (status) {
    case 200:
      return "OK";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 408:
      return "Request Timeout";
    case 422:
      return "Unprocessable Content";
    case 431:
      return "Request Header Fields Too Large";
    default:
      return "Internal Server Error";
  }
}

bool WebServer::requestTimedOut(uint32_t now) const {
  return isReadingRequest() &&
         hasElapsed(now, lastClientActivityAt_,
                    Config::HTTP_REQUEST_TIMEOUT_MS);
}

bool WebServer::isReadingRequest() const {
  return clientState_ == ClientState::readingHeaders ||
         clientState_ == ClientState::readingMultipartHeaders ||
         clientState_ == ClientState::receivingUpload;
}

void WebServer::closeClient() {
  if (clientState_ == ClientState::receivingUpload) {
    firmwareUpdater_.cancelUpload();
  }
  backlogReader_.close();
  client_.stop();
  clientState_ = ClientState::idle;
  requestHeaders_ = "";
  multipartHeaders_ = "";
  boundary_ = "";
  responseBody_ = "";
  responseOffset_ = 0;
  restartAfterResponse_ = false;
  streamingApiHistory_ = false;
}
