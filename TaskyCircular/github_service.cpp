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

  // Optimization: Fetch only 1 event to check "Last Activity"
  String url = "https://api.github.com/users/" + model->cfg.githubUser +
               "/events?per_page=1";

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
  // Goal: Check if the latest event is "Today".
  // JSON format: [ { "created_at": "YYYY-MM-DDThh:mm:ssZ", ... } ]

  int createdIdx = data.indexOf("\"created_at\":\"");
  if (createdIdx > 0) {
    String dateStr =
        data.substring(createdIdx + 14, createdIdx + 24); // YYYY-MM-DD
    Serial.println("[GH] Last Event: " + dateStr);

    // Compare with system time
    struct tm now;
    if (getLocalTime(&now)) {
      char buf[12];
      snprintf(buf, sizeof(buf), "%04d-%02d-%02d", now.tm_year + 1900,
               now.tm_mon + 1, now.tm_mday);
      String today = String(buf);

      bool activeToday = (dateStr == today);

      // Update Model
      // Logic: If activeToday is true, ensure currentDayIndex in streak is
      // marked? Or just visual feedback? model->updateStreak(activeToday); //
      // method to add
      Serial.println(activeToday ? "[GH] Active Today!"
                                 : "[GH] No activity yet today");

      // For now, let's hack the streak visual
      if (activeToday) {
        // Mark the current cursor or day as '3' (Green)
        // We might need a proper "map date to index" logic later for true
        // scalability
        model->streak[model->currentDayIndex] = 3;
      }
    }
  }
}
