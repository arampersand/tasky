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
      // Configure NTP (non-blocking setup)
      configTime(model->cfg.timezone * 3600, 0, "pool.ntp.org", "time.nist.gov");
      syncStartAt = millis();
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
      // Non-blocking time sync check
      if (!timeSynced) {
        checkTimeSync();
      }
    }
  }
}

// Non-blocking time sync check - call repeatedly until synced
void TaskyNetManager::checkTimeSync() {
  // Check if time is available (non-blocking)
  time_t now = time(nullptr);

  // time() returns seconds since epoch. If > year 2020, NTP worked
  if (now > 1577836800) {  // Jan 1, 2020
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);  // Non-blocking conversion

    model->clockH = timeinfo.tm_hour;
    model->clockM = timeinfo.tm_min;
    model->clockS = timeinfo.tm_sec;
    model->clockSetAtMs = millis();
    timeSynced = true;
    Serial.println("Time Synced!");
    Serial.printf("Time: %02d:%02d:%02d\n", model->clockH, model->clockM, model->clockS);
  } else {
    // Not synced yet - check if timeout (give NTP 10 seconds max)
    if (millis() - syncStartAt > 10000) {
      Serial.println("NTP sync timeout - using fallback time");
      timeSynced = true;  // Give up, use default time
    }
    // Otherwise, just return and let loop continue - NTP will sync in background
  }
}

// Legacy function - kept for compatibility but now non-blocking
void TaskyNetManager::syncTime() {
  // NTP is already configured in begin(), just trigger a check
  checkTimeSync();
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
