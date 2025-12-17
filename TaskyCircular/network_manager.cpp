#include "network_manager.h"

TaskyNetManager::TaskyNetManager(TaskyModel *m) : model(m) {}

void TaskyNetManager::begin() {
  if (model->cfg.wifiSSID.length() == 0) {
    setState(NET_DISCONNECTED);
    return;
  }

  // Ensure we don't break AP if it's running
  // If AP is on, use AP_STA, else STA
  WiFi.mode(WIFI_AP_STA);

  Serial.print("Connecting to: ");
  Serial.println(model->cfg.wifiSSID);

  WiFi.begin(model->cfg.wifiSSID.c_str(), model->cfg.wifiPass.c_str());

  connectionStartAt = millis();
  retryCount = 0;
  setState(NET_CONNECTING);
}

void TaskyNetManager::disconnect() {
  WiFi.disconnect();
  setState(NET_DISCONNECTED);
}

void TaskyNetManager::loop() {
  // Simple state machine
  if (state == NET_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi Connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      setState(NET_CONNECTED);
    } else {
      // Timeout check (15 seconds)
      if (millis() - connectionStartAt > 15000) {
        Serial.println("WiFi Connect Timeout");
        setState(NET_FAIL);
        WiFi.disconnect(); // Stop trying
      }
    }
  } else if (state == NET_CONNECTED) {
    // Monitor for drop
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi Lost...");
      setState(NET_DISCONNECTED);
      timeSynced = false;
    } else {
      // If just connected and not synced, try sync
      if (!timeSynced) {
        syncTime();
      }
    }
  }
}

void TaskyNetManager::syncTime() {
  Serial.println("Syncing Time...");
  // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer)
  // Timezone is hours. Sec = tz * 3600
  long gmtOffset_sec = model->cfg.timezone * 3600;
  int daylightOffset_sec = 0; // No DST logic for now or manually managed?

  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.nis.gov");

  struct tm timeinfo;
  // Wait for time (blocking briefly or just check?)
  // getLocalTime blocks for up to timeout (default 5000ms)
  if (getLocalTime(&timeinfo)) {
    Serial.println("Time Synced!");
    model->clockH = timeinfo.tm_hour;
    model->clockM = timeinfo.tm_min;
    model->clockS = timeinfo.tm_sec;
    model->clockSetAtMs = millis();
    timeSynced = true;
  } else {
    Serial.println("Time Sync Failed");
  }
}

bool TaskyNetManager::isConnected() {
  return (state == NET_CONNECTED && WiFi.status() == WL_CONNECTED);
}

String TaskyNetManager::getIP() {
  if (!isConnected())
    return "0.0.0.0";
  return WiFi.localIP().toString();
}

void TaskyNetManager::setState(NetworkState s) { state = s; }

String TaskyNetManager::getStatusString() {
  switch (state) {
  case NET_DISCONNECTED:
    return "Offline";
  case NET_CONNECTING:
    return "Connecting...";
  case NET_CONNECTED:
    return "Online";
  case NET_FAIL:
    return "Failed";
  default:
    return "?";
  }
}
