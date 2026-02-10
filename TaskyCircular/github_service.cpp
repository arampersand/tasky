#include "config_features.h"
#if FEATURE_GITHUB
#include "github_service.h"
#include <Arduino.h>

GithubService::GithubService(TaskyModel *m) : model(m) {}

void GithubService::begin() {}

void GithubService::loop() {
  if (model->cfg.wifiSSID.length() == 0 || model->cfg.githubUser.length() == 0)
    return;

  // Update every 30 minutes
  if (millis() - lastUpdate > 30 * 60 * 1000) {
    forceUpdate();
  }
}

void GithubService::forceUpdate() {
  if (model->cfg.githubUser.length() == 0)
    return;

  Serial.println("[GH] Updating...");
  lastUpdate = millis();

  // Fetch more events to count today's contributions
  String url = "https://api.github.com/users/" + model->cfg.githubUser +
               "/events?per_page=30";

  // Note: Public events don't strictly need a token, but rate limits are higher
  // with one. We'll use the token if provided.
  String payload = api.get(url, model->cfg.githubToken);

  if (payload.length() > 0) {
    parseContribs(payload);
  } else {
    Serial.println("[GH] Failed");
  }
}

void GithubService::parseContribs(String data) {
  // Count all events from today
  // JSON format: [ { "created_at": "YYYY-MM-DDThh:mm:ssZ", ... }, ... ]

  // Get today's date string
  time_t nowTime = time(nullptr);
  if (nowTime < 1577836800) {
    // No valid time yet - NTP hasn't synced
    return;
  }

  struct tm now;
  localtime_r(&nowTime, &now);

  char buf[12];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", now.tm_year + 1900,
           now.tm_mon + 1, now.tm_mday);
  String today = String(buf);

  // Count events from today
  int count = 0;
  int searchStart = 0;

  while (true) {
    int idx = data.indexOf("\"created_at\":\"", searchStart);
    if (idx < 0) break;

    String dateStr = data.substring(idx + 14, idx + 24);  // YYYY-MM-DD
    if (dateStr == today) {
      count++;
    }
    searchStart = idx + 24;
  }

  // Store the count and update streak
  model->cfg.githubContribCount = count;
  model->updateStreakIntensity();

  Serial.printf("[GH] Today's contributions: %d\n", count);
}
#endif
