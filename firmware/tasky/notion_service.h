#pragma once

#include "api_client.h"
#include "model.h"

class NotionService {
public:
  NotionService(TaskyModel* model);

  void begin();
  void loop();      // Call periodically
  void forceUpdate();

private:
  TaskyModel* model;
  ApiClient api;
  uint32_t lastUpdate = 0;

  void parseResponse(const String& payload);
  String extractJsonString(const String& json, const String& key, int startPos = 0);
  bool extractJsonBool(const String& json, const String& key, int startPos = 0);
};
