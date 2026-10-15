#include "config_features.h"
#if FEATURE_FIREBASE
#include "firebase_service.h"
#include <WiFi.h>

FirebaseService::FirebaseService(TaskyModel *m) : model(m) {
  api.setInsecure();  // Skip cert validation for simplicity
}

void FirebaseService::begin() {
  if (!isConfigured()) return;

  deviceId = generateDeviceId();
  Serial.printf("[Firebase] Device ID: %s\n", deviceId.c_str());

  // Initial sync
  forceSync();
}

void FirebaseService::loop() {
  if (!isConfigured() || !model->cfg.firebaseEnabled) return;

  // Check if it's time to sync
  uint32_t now = millis();
  uint32_t interval = (lastSyncTimestamp > 0) ? SYNC_INTERVAL_MS : RETRY_INTERVAL_MS;

  if (now - lastSync > interval) {
    forceSync();
  }
}

void FirebaseService::forceSync() {
  if (!isConfigured() || syncing) return;

  syncing = true;
  lastSync = millis();

  Serial.println("[Firebase] Syncing...");

  // Step 1: Update device info
  syncDeviceInfo();

  // Step 2: Sync daily summary
  syncDailySummary();

  // Step 3: Sync stats
  syncStats();

  // Step 4: Process any pending offline data
  processPendingQueue();

  syncing = false;
  Serial.println("[Firebase] Sync complete");
}

bool FirebaseService::isConfigured() const {
  return model->cfg.firebaseUrl.length() > 0 &&
         model->cfg.firebaseSecret.length() > 0;
}

bool FirebaseService::isSyncing() const {
  return syncing;
}

String FirebaseService::getDeviceId() const {
  return deviceId;
}

String FirebaseService::getLastSyncTime() const {
  if (lastSyncTimestamp == 0) return "Never";

  uint32_t elapsed = (millis() - lastSync) / 1000;
  if (elapsed < 60) return String(elapsed) + "s ago";
  if (elapsed < 3600) return String(elapsed / 60) + "m ago";
  return String(elapsed / 3600) + "h ago";
}

String FirebaseService::generateDeviceId() {
  uint8_t mac[6];
  WiFi.macAddress(mac);

  char id[32];
  snprintf(id, sizeof(id), "tasky_%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(id);
}

String FirebaseService::buildFirebaseUrl(const String &path) {
  String url = "https://";
  url += model->cfg.firebaseUrl;
  if (!model->cfg.firebaseUrl.endsWith(".firebaseio.com")) {
    url += ".firebaseio.com";
  }
  url += path;
  url += ".json?auth=";
  url += model->cfg.firebaseSecret;
  return url;
}

bool FirebaseService::firebasePut(const String &path, const String &json) {
  String url = buildFirebaseUrl(path);

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int code = http.PUT(json);
  String response = http.getString();
  http.end();

  if (code == 200) {
    lastSyncTimestamp = millis();
    return true;
  }

  Serial.printf("[Firebase] PUT failed: %d - %s\n", code, response.c_str());
  return false;
}

bool FirebaseService::firebasePost(const String &path, const String &json) {
  String url = buildFirebaseUrl(path);

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(json);
  http.end();

  return (code == 200);
}

String FirebaseService::firebaseGet(const String &path) {
  String url = buildFirebaseUrl(path);
  return api.get(url);
}

void FirebaseService::syncDeviceInfo() {
  String path = "/devices/" + deviceId + "/info";

  String json = "{";
  json += "\"lastSeen\":" + String(time(nullptr)) + ",";
  json += "\"firmware\":\"2.1.0\",";
  json += "\"name\":\"" + model->cfg.wifiSSID + "\"";
  json += "}";

  firebasePut(path, json);
}

void FirebaseService::syncDailySummary() {
  String today = getTodayDateString();
  if (today.length() == 0) return;  // No valid time yet

  String path = "/devices/" + deviceId + "/sessions/" + today;

  // Calculate today's intensity
  uint8_t intensity = 0;
  if (model->cfg.streakSource == STREAK_GITHUB) {
    intensity = model->cfg.githubContribCount;
  } else if (model->cfg.streakSource == STREAK_TASKY) {
    intensity = model->cfg.dailyPomodoroCount;
  } else {
    intensity = max(model->cfg.githubContribCount, model->cfg.dailyPomodoroCount);
  }

  String json = "{";
  json += "\"pomodoroCount\":" + String(model->cfg.dailyPomodoroCount) + ",";
  json += "\"focusMin\":" + String(model->cfg.dailyPomodoroCount * model->cfg.workMin) + ",";
  json += "\"intensity\":" + String(intensity);
  json += "}";

  firebasePut(path, json);
}

void FirebaseService::syncStats() {
  String path = "/devices/" + deviceId + "/stats";

  String json = "{";
  json += "\"totalFocusMin\":" + String(model->cfg.totalFocusMin) + ",";
  json += "\"bestStreak\":" + String(model->cfg.bestStreak) + ",";
  json += "\"currentStreak\":" + String(model->calculateCurrentStreak());
  json += "}";

  firebasePut(path, json);
}

void FirebaseService::processPendingQueue() {
  // Process any offline queued data
  for (int i = 0; i < pendingCount; i++) {
    DailySummary &s = pendingQueue[i];
    String path = "/devices/" + deviceId + "/sessions/" + String(s.date);

    String json = "{";
    json += "\"pomodoroCount\":" + String(s.pomodoroCount) + ",";
    json += "\"focusMin\":" + String(s.focusMinutes) + ",";
    json += "\"intensity\":" + String(s.streakIntensity);
    json += "}";

    if (firebasePut(path, json)) {
      Serial.printf("[Firebase] Synced pending: %s\n", s.date);
    }
  }
  pendingCount = 0;  // Clear queue after processing
}

String FirebaseService::getTodayDateString() {
  time_t nowTime = time(nullptr);
  if (nowTime < 1577836800) {
    // No valid time yet (before 2020)
    return "";
  }

  struct tm now;
  localtime_r(&nowTime, &now);

  char buf[12];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
           now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);
  return String(buf);
}
#endif
