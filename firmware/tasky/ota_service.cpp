#include "ota_service.h"

#include "version.h"

const char* OtaService::FIRMWARE_VERSION = TASKY_VERSION;

OtaService::OtaService(TaskyModel *m) : model(m) {}

void OtaService::begin() {
  state = OTA_IDLE;
  progress = 0;
  errorMsg = "";
}

void OtaService::loop() {
  // OTA is driven by WiFiPortal callbacks, nothing to do here
}

bool OtaService::startUpdate(size_t totalSize) {
  if (!isArmed()) {
    errorMsg = "Enable Update on the Tasky first (More > Update)";
    state = OTA_ERROR;
    return false;
  }
  // A previous failed attempt must not block retries
  if (state == OTA_RECEIVING || state == OTA_WRITING) {
    errorMsg = "Update already in progress";
    return false;
  }

  totalBytes = totalSize;
  writtenBytes = 0;
  progress = 0;
  state = OTA_RECEIVING;
  errorMsg = "";

  Serial.printf("[OTA] Starting update, size: %d bytes\n", totalSize);

  // The exact image size isn't known until the upload ends: let Update use the
  // whole OTA slot (passing a larger guess would fail with "not enough space").
  if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
    errorMsg = "Not enough space for update";
    state = OTA_ERROR;
    Update.printError(Serial);
    return false;
  }

  state = OTA_WRITING;
  return true;
}

bool OtaService::writeChunk(uint8_t *data, size_t len) {
  if (state != OTA_WRITING) {
    errorMsg = "Not in writing state";
    return false;
  }

  size_t written = Update.write(data, len);
  if (written != len) {
    errorMsg = "Write error";
    state = OTA_ERROR;
    Update.printError(Serial);
    return false;
  }

  writtenBytes += written;
  // totalBytes is an estimate (may be 0): stay below 100% until finishUpdate()
  progress = totalBytes ? min<size_t>(99, (writtenBytes * 100) / totalBytes) : 0;

  // Print progress every 10%
  static int lastReportedProgress = -10;
  if (progress - lastReportedProgress >= 10) {
    Serial.printf("[OTA] Progress: %d%%\n", progress);
    lastReportedProgress = progress;
  }

  return true;
}

bool OtaService::finishUpdate() {
  if (state == OTA_ERROR)
    return false; // Keep the error that stopped the upload
  if (state != OTA_WRITING) {
    errorMsg = "Not in writing state";
    return false;
  }

  if (!Update.end(true)) {
    errorMsg = "Update failed to finalize";
    state = OTA_ERROR;
    Update.printError(Serial);
    return false;
  }

  state = OTA_SUCCESS;
  progress = 100;
  Serial.println("[OTA] Update successful!");
  return true;
}

void OtaService::abortUpdate() {
  if (state == OTA_WRITING || state == OTA_RECEIVING) {
    Update.abort();
  }
  state = OTA_IDLE;
  progress = 0;
  writtenBytes = 0;
  errorMsg = "Update aborted";
  Serial.println("[OTA] Update aborted");
}

void OtaService::rebootDevice() {
  Serial.println("[OTA] Rebooting...");
  delay(500);
  ESP.restart();
}
