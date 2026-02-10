#pragma once

#include "model.h"
#include <Arduino.h>
#include <Update.h>

// OTA Update states
enum OtaState {
  OTA_IDLE = 0,
  OTA_RECEIVING,
  OTA_WRITING,
  OTA_SUCCESS,
  OTA_ERROR
};

class OtaService {
public:
  OtaService(TaskyModel *model);

  void begin();
  void loop();

  // Called by WiFiPortal when upload starts
  bool startUpdate(size_t totalSize);
  // Called for each chunk of data
  bool writeChunk(uint8_t *data, size_t len);
  // Called when upload completes
  bool finishUpdate();
  // Called on upload error
  void abortUpdate();

  // State accessors
  OtaState getState() const { return state; }
  int getProgress() const { return progress; }
  String getError() const { return errorMsg; }
  String getVersion() const { return FIRMWARE_VERSION; }

  // Reboot after successful update
  void rebootDevice();

private:
  TaskyModel *model;
  OtaState state = OTA_IDLE;
  int progress = 0;
  size_t totalBytes = 0;
  size_t writtenBytes = 0;
  String errorMsg = "";

  static const char* FIRMWARE_VERSION;
};
