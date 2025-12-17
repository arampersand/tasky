#pragma once

#include "model.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

class TaskyNetManager; // Forward declaration

class WiFiPortal {
public:
  WebServer server;
  DNSServer dnsServer;
  TaskyModel *model;
  TaskyNetManager *net; // [NEW]
  bool wifiOn = false;
  String apSSID = "Tasky";
  String apPass = "GetItDone";

  WiFiPortal(TaskyModel *m, TaskyNetManager *n); // [UPDATED]

  void begin();
  void stop();
  void loop();

private:
  void handleRoot();
  void handleSave();
  String htmlPage();
};
