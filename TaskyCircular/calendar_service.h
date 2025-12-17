#pragma once

#include "api_client.h"
#include "model.h"

class CalendarService {
public:
  CalendarService(TaskyModel *model);

  void begin();
  void loop(); // Call periodically
  void forceUpdate();

private:
  TaskyModel *model;
  ApiClient api;
  uint32_t lastUpdate = 0;
  bool updating = false;

  void parseICal(String data);
};
