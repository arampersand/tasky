#include "config_features.h"
#if FEATURE_GITHUB
#include "github_service.h"
#include <Arduino.h>

GithubService::GithubService(TaskyModel *m) : model(m) {}

void GithubService::begin() {}

void GithubService::loop() {
  if (model->cfg.wifiSSID.length() == 0 || model->cfg.githubUser.length() == 0)
    return;

  // First update: wait 10s for WiFi, then every 30 minutes
  uint32_t interval = (lastUpdate == 0) ? 10000 : (30UL * 60 * 1000);
  if (millis() - lastUpdate > interval) {
    forceUpdate();
  }
}

void GithubService::forceUpdate() {
  if (model->cfg.githubUser.length() == 0)
    return;

  Serial.println("[GH] Updating...");

  // Fetch 100 events for better 30-day coverage
  String url = "https://api.github.com/users/" + model->cfg.githubUser +
               "/events?per_page=100";

  String payload = api.get(url, model->cfg.githubToken);

  if (payload.length() > 0) {
    parseContribs(payload);
    lastUpdate = millis();  // Only set on success
  } else {
    Serial.println("[GH] Failed - will retry in 30s");
    lastUpdate = millis() - (30UL * 60 * 1000) + 30000;  // Retry in 30s
  }
}

void GithubService::parseContribs(String data) {
  // Parse ALL event dates and map to last 30 days for streak grid
  time_t nowTime = time(nullptr);
  if (nowTime < 1577836800) return;  // NTP not synced

  struct tm now;
  localtime_r(&nowTime, &now);

  // Count events per day for last 30 days
  // dailyCounts[0] = today, [1] = yesterday, etc.
  int dailyCounts[30] = {0};

  int searchStart = 0;
  int totalEvents = 0;

  while (true) {
    int idx = data.indexOf("\"created_at\":\"", searchStart);
    if (idx < 0) break;

    // Parse YYYY-MM-DD from created_at
    String dateStr = data.substring(idx + 14, idx + 24);
    int eYear = dateStr.substring(0, 4).toInt();
    int eMonth = dateStr.substring(5, 7).toInt();
    int eDay = dateStr.substring(8, 10).toInt();

    // Calculate days ago using mktime
    struct tm eventTm = {0};
    eventTm.tm_year = eYear - 1900;
    eventTm.tm_mon = eMonth - 1;
    eventTm.tm_mday = eDay;
    eventTm.tm_hour = 12;  // Noon to avoid DST edge cases
    time_t eventTime = mktime(&eventTm);

    // Use midnight of today for comparison
    struct tm todayTm = now;
    todayTm.tm_hour = 12;
    todayTm.tm_min = 0;
    todayTm.tm_sec = 0;
    time_t todayTime = mktime(&todayTm);

    int daysAgo = (todayTime - eventTime) / 86400;
    if (daysAgo >= 0 && daysAgo < 30) {
      dailyCounts[daysAgo]++;
    }

    totalEvents++;
    searchStart = idx + 24;
  }

  // Map to streak grid (currentDayIndex = today, backwards)
  for (int d = 0; d < 30 && d < STREAK_N; d++) {
    int streakIdx = (model->currentDayIndex - d + STREAK_N) % STREAK_N;

    // Convert count to intensity (0-3)
    uint8_t ghIntensity = 0;
    if (dailyCounts[d] >= 8) ghIntensity = 3;
    else if (dailyCounts[d] >= 4) ghIntensity = 2;
    else if (dailyCounts[d] >= 1) ghIntensity = 1;

    // Combine with existing intensity (keep max for STREAK_BOTH)
    if (model->cfg.streakSource == STREAK_GITHUB) {
      model->streak[streakIdx] = ghIntensity;
    } else if (model->cfg.streakSource == STREAK_BOTH) {
      if (ghIntensity > model->streak[streakIdx]) {
        model->streak[streakIdx] = ghIntensity;
      }
    }
    // STREAK_TASKY: don't touch streak from GitHub
  }

  // Store today's count for updateStreakIntensity()
  model->cfg.githubContribCount = dailyCounts[0];
  model->updateStreakIntensity();
  model->save();

  Serial.printf("[GH] Parsed %d events, today: %d contributions\n",
                totalEvents, dailyCounts[0]);
}
#endif
