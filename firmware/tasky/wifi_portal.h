#pragma once

#include "model.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

class TaskyNetManager; // Forward declaration
class OtaService;      // Forward declaration

class WiFiPortal {
public:
  WebServer server;
  DNSServer dns;        // Captive portal DNS (resolves everything to the AP)
  OtaService* ota;      // OTA update service
  TaskyModel *model;
  TaskyNetManager *net;
  bool wifiOn = false;
  String apSSID = "Tasky";
  String apPass = "GetItDone";

  WiFiPortal(TaskyModel *m, TaskyNetManager *n, OtaService* otaSvc);

  void begin();
  void stop();
  void loop();

  String getUptimeString();

private:
  void handleRoot();
  void handleSave();
  void handleOtaUpload();
  void handleOtaStatus();
  String htmlPage();
};
