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

// The contribution graph on your profile page (the same one github.com
// shows, private contributions included when your profile shows them). One
// line per day: <td ... data-date="2026-10-16" ... data-level="0-4" ...>
// The public events API only had public activity, so most days were missing.
struct GhParse {
  time_t today;  // noon today
  uint8_t level[STREAK_N];
  int days;      // cells parsed
};

static bool parseContribLine(const char *line, void *user) {
  GhParse *p = (GhParse *)user;
  const char *d = strstr(line, "data-date=\"");
  const char *l = d ? strstr(d, "data-level=\"") : nullptr;
  if (!l)
    return true;
  d += 11;
  struct tm t = {0};
  t.tm_year = atoi(d) - 1900;
  t.tm_mon = atoi(d + 5) - 1;
  t.tm_mday = atoi(d + 8);
  t.tm_hour = 12;  // noon: no DST edge cases
  // + half a day: a DST hour can't round a day away
  int daysAgo = (int)((p->today - mktime(&t) + 43200) / 86400);
  int level = l[12] - '0';
  if (level < 0 || level > 4)
    return true;
  level = level > 3 ? 3 : level;  // GitHub has 4 shades, Tasky 3
  // The page counts days in UTC: west of it, tonight's work is "tomorrow"
  if (daysAgo == -1)
    p->level[0] = max(p->level[0], (uint8_t)level);
  else if (daysAgo >= 0 && daysAgo < STREAK_N)
    p->level[daysAgo] = max(p->level[daysAgo], (uint8_t)level);
  p->days++;
  return true;
}

void GithubService::forceUpdate() {
  if (model->cfg.githubUser.length() == 0)
    return;
  time_t nowTime = time(nullptr);
  if (nowTime < 1577836800)
    return;  // NTP not synced

  Serial.println("[GH] Updating...");
  GhParse p = {};
  struct tm now;
  localtime_r(&nowTime, &now);
  now.tm_hour = 12;
  now.tm_min = now.tm_sec = 0;
  p.today = mktime(&now);

  // ~230 KB of HTML for a year of cells, read line by line
  bool ok = api.getStreaming("https://github.com/users/" + model->cfg.githubUser + "/contributions",
                             parseContribLine, &p, "", 400000);
  if (!ok || p.days < STREAK_N) {
    Serial.printf("[GH] Failed (%d days) - will retry in 30s\n", p.days);
    lastUpdate = millis() - (30UL * 60 * 1000) + 30000;  // Retry in 30s
    return;
  }
  lastUpdate = millis();

  // The GitHub grid: currentDayIndex = today, backwards. Your own grid is
  // Tasky activity only; the Streak screen shows both.
  for (int d = 0; d < STREAK_N; d++)
    model->ghStreak[(model->currentDayIndex - d + STREAK_N) % STREAK_N] = p.level[d];

  // updateStreakIntensity() turns today's count back into the same shade
  static const uint8_t COUNT_FOR_LEVEL[4] = {0, 1, 4, 8};
  model->cfg.githubContribCount = COUNT_FOR_LEVEL[p.level[0]];
  model->updateStreakIntensity();
  model->save();
  Serial.printf("[GH] %d days parsed, today level %d\n", p.days, p.level[0]);
}
#endif
