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

  // The setup AP + web server shut down after this long without activity
  // (HTTP requests, connected phones, or a setup screen open on the device).
  static const uint32_t IDLE_STOP_MS = 10UL * 60UL * 1000UL;

  WiFiPortal(TaskyModel *m, TaskyNetManager *n, OtaService* otaSvc);

  void begin();
  void stop();
  void loop();
  // Call every loop while a setup screen (QR / WiFi info / Update) is shown.
  void touch() { lastActivityAt = millis(); }

  String getUptimeString();

private:
  bool routesAdded = false;
  uint32_t lastActivityAt = 0;

  void addRoutes();
  void handleRoot();
  void handleSave();
  void handleOtaUpload();
  void handleOtaStatus();
  String htmlPage();
  // Secrets are never sent back to the browser: the field renders empty, an
  // empty submit keeps the stored value, and a checkbox clears it.
  String secretField(const char *label, const char *name, const String &stored,
                     const char *placeholder, const char *type = "password");
  void applySecret(const char *name, String &stored);
};
