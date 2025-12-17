#pragma once

#include "model.h"
#include <Arduino.h>
#include <WiFi.h>

enum NetworkState { NET_DISCONNECTED, NET_CONNECTING, NET_CONNECTED, NET_FAIL };

class TaskyNetManager {
public:
  TaskyModel *model;
  NetworkState state = NET_DISCONNECTED;

  // Connection tracking
  uint32_t lastCheckAt = 0;
  uint32_t connectionStartAt = 0;
  int retryCount = 0;
  const int MAX_RETRIES = 3;

  TaskyNetManager(TaskyModel *m);

  void begin(); // Start connection attempt if configured
  void loop();  // Monitor status
  void disconnect();

  bool isConnected();
  String getIP();
  String getStatusString();

  void syncTime(); // [NEW] Fetch NTP time

private:
  void setState(NetworkState s);
  bool timeSynced = false;
};
