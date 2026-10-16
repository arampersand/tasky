#pragma once

#include "api_client.h"
#include "model.h"

class GithubService {
public:
  GithubService(TaskyModel *model);

  void begin();
  void loop();
  void forceUpdate();

private:
  TaskyModel *model;
  ApiClient api;
  uint32_t lastUpdate = 0;

};
