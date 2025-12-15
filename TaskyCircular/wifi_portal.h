#pragma once

#include "model.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

// Managed by main sketch mostly, but logic here
class WiFiPortal {
public:
  WebServer server;
  DNSServer dnsServer;
  TaskyModel *model;
  bool wifiOn = false;
  String apSSID = "Tasky";
  String apPass = "EverySecondCounts";

  WiFiPortal(TaskyModel *m);

  void begin();
  void stop();
  void loop();

private:
  void handleRoot();
  void handleSave();
  String htmlPage();
};
