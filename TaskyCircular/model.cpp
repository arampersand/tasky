#include "model.h"
#include "config_features.h"
#include <time.h>  // For getLocalTime()

void TaskyModel::begin() { load(); }

uint8_t TaskyModel::clampU8(int v, int lo, int hi) {
  if (v < lo)
    return (uint8_t)lo;
  if (v > hi)
    return (uint8_t)hi;
  return (uint8_t)v;
}

void TaskyModel::load() {
  pref.begin("tasky", true);
  cfg.workMin = pref.getUChar("work", 25);
  cfg.breakMin = pref.getUChar("break", 5);
  cfg.screenBright = pref.getUChar("bl", 200);
  cfg.ledBright = pref.getUChar("led", 180);
  cfg.soundOn = pref.getBool("snd", true);
  cfg.darkMode = pref.getBool("dark", true);
  cfg.totalFocusMin = pref.getUInt("focus", 0);
  cfg.bestStreak = pref.getUShort("best", 0);
  cfg.wifiSSID = pref.getString("wss", "");
  cfg.wifiPass = pref.getString("wps", "");
  cfg.timezone = pref.getChar("tz", -5);
  cfg.githubUser = pref.getString("ghu", "");
  cfg.githubToken = pref.getString("ght", "");
  cfg.calendarUrl = pref.getString("cal", "");

  // Notion Integration
  cfg.notionApiKey = pref.getString("nkey", "");
  cfg.notionDatabaseId = pref.getString("ndb", "");

  // Focus Mode
  cfg.focusAutoMode = pref.getBool("fAuto", true);
  cfg.focusManualMode = pref.getBool("fMan", false);

  // Streak Settings
  cfg.streakSource = (StreakSource)pref.getUChar("sks", STREAK_BOTH);
  cfg.dailyPomodoroCount = pref.getUChar("dpc", 0);
  cfg.githubContribCount = pref.getUChar("ghc", 0);

  // Firebase Cloud Sync
  cfg.firebaseUrl = pref.getString("fbUrl", "");
  cfg.firebaseSecret = pref.getString("fbKey", "");
  cfg.firebaseEnabled = pref.getBool("fbOn", false);

  // Weather Widget (Open-Meteo)
  cfg.weatherCity = pref.getString("wCity", "");
  cfg.weatherCelsius = pref.getBool("wCel", true);
  cfg.weatherLat = pref.getFloat("wLat", 0);
  cfg.weatherLon = pref.getFloat("wLon", 0);

  // Spotify Integration
  cfg.spotifyClientId = pref.getString("spId", "");
  cfg.spotifySecret = pref.getString("spSec", "");
  cfg.spotifyToken = pref.getString("spTok", "");
  cfg.spotifyRefresh = pref.getString("spRef", "");

  // LLM Agent
  cfg.llmApiKey = pref.getString("lKey", "");
  cfg.llmProvider = pref.getUChar("lPrv", 0);
  cfg.llmModeOverride = pref.getChar("lMod", -1);

  for (int i = 0; i < 3; i++) {
    String key = "t" + String(i);
    tasks[i] = pref.getString(key.c_str(), "Empty");
    String keyd = "d" + String(i);
    taskDone[i] = pref.getBool(keyd.c_str(), false);
    String keyDesc = "td" + String(i);
    taskDescription[i] = pref.getString(keyDesc.c_str(), "");
  }

  // Calendar events (reset on load, will be populated by calendar service)
  for (int i = 0; i < 31; i++) {
    calendarEvents[i] = 0;
  }

  // Clear today's/month events and Notion tasks (will be populated by services)
  clearTodayEvents();
  clearMonthEvents();
  notionTaskCount = 0;
  memset(notionTasks, 0, sizeof(notionTasks));

  for (int i = 0; i < STREAK_N; i++) {
    String k = "s" + String(i);
    streak[i] = pref.getUChar(k.c_str(), 0);
  }
  currentDayIndex = (int)pref.getUChar("day", 0);
  if (currentDayIndex < 0 || currentDayIndex >= STREAK_N)
    currentDayIndex = 0;
  streakCursor = currentDayIndex;

  clockH = (int)pref.getUChar("ch", 12);
  clockM = (int)pref.getUChar("cm", 0);
  clockS = (int)pref.getUChar("cs", 0);
  pref.end();

  // limits
  cfg.workMin = clampU8(cfg.workMin, 5, 90);
  cfg.breakMin = clampU8(cfg.breakMin, 5, 90);
  if (cfg.screenBright < 20)
    cfg.screenBright = 20;
  if (clockH < 0 || clockH > 23)
    clockH = 12;
  if (clockM < 0 || clockM > 59)
    clockM = 0;
  if (clockS < 0 || clockS > 59)
    clockS = 0;

  // Apply compile-time defaults from config_secrets.h when NVS is empty
  if (cfg.wifiSSID.length() == 0 && strlen(SECRET_WIFI_SSID) > 0)
    cfg.wifiSSID = SECRET_WIFI_SSID;
  if (cfg.wifiPass.length() == 0 && strlen(SECRET_WIFI_PASS) > 0)
    cfg.wifiPass = SECRET_WIFI_PASS;
  if (cfg.llmApiKey.length() == 0 && strlen(SECRET_LLM_API_KEY) > 0)
    cfg.llmApiKey = SECRET_LLM_API_KEY;
  if (cfg.llmProvider == 0 && strlen(SECRET_LLM_PROVIDER) > 0) {
    String prov = SECRET_LLM_PROVIDER;
    if (prov == "openai") cfg.llmProvider = 1;
  }
  if (cfg.weatherCity.length() == 0 && strlen(SECRET_WEATHER_CITY) > 0)
    cfg.weatherCity = SECRET_WEATHER_CITY;
  if (cfg.weatherLat == 0 && SECRET_WEATHER_LAT != 0)
    cfg.weatherLat = SECRET_WEATHER_LAT;
  if (cfg.weatherLon == 0 && SECRET_WEATHER_LON != 0)
    cfg.weatherLon = SECRET_WEATHER_LON;
  if (cfg.notionApiKey.length() == 0 && strlen(SECRET_NOTION_API_KEY) > 0)
    cfg.notionApiKey = SECRET_NOTION_API_KEY;
  if (cfg.notionDatabaseId.length() == 0 && strlen(SECRET_NOTION_DATABASE_ID) > 0)
    cfg.notionDatabaseId = SECRET_NOTION_DATABASE_ID;
  if (cfg.githubUser.length() == 0 && strlen(SECRET_GITHUB_USER) > 0)
    cfg.githubUser = SECRET_GITHUB_USER;
  if (cfg.githubToken.length() == 0 && strlen(SECRET_GITHUB_TOKEN) > 0)
    cfg.githubToken = SECRET_GITHUB_TOKEN;
  if (cfg.calendarUrl.length() == 0 && strlen(SECRET_CALENDAR_URL) > 0)
    cfg.calendarUrl = SECRET_CALENDAR_URL;
  if (cfg.firebaseUrl.length() == 0 && strlen(SECRET_FIREBASE_URL) > 0)
    cfg.firebaseUrl = SECRET_FIREBASE_URL;
  if (cfg.firebaseSecret.length() == 0 && strlen(SECRET_FIREBASE_SECRET) > 0)
    cfg.firebaseSecret = SECRET_FIREBASE_SECRET;
  if (cfg.spotifyClientId.length() == 0 && strlen(SECRET_SPOTIFY_CLIENT_ID) > 0)
    cfg.spotifyClientId = SECRET_SPOTIFY_CLIENT_ID;
  if (cfg.spotifySecret.length() == 0 && strlen(SECRET_SPOTIFY_SECRET) > 0)
    cfg.spotifySecret = SECRET_SPOTIFY_SECRET;
}

void TaskyModel::save() {
  pref.begin("tasky", false);
  pref.putUChar("work", cfg.workMin);
  pref.putUChar("break", cfg.breakMin);
  pref.putUChar("bl", cfg.screenBright);
  pref.putUChar("led", cfg.ledBright);
  pref.putBool("snd", cfg.soundOn);
  pref.putBool("dark", cfg.darkMode);
  pref.putUInt("focus", cfg.totalFocusMin);
  pref.putUShort("best", cfg.bestStreak);
  pref.putString("wss", cfg.wifiSSID);
  pref.putString("wps", cfg.wifiPass);
  pref.putChar("tz", cfg.timezone);
  pref.putString("ghu", cfg.githubUser);
  pref.putString("ght", cfg.githubToken);
  pref.putString("cal", cfg.calendarUrl);

  // Notion Integration
  pref.putString("nkey", cfg.notionApiKey);
  pref.putString("ndb", cfg.notionDatabaseId);

  // Focus Mode
  pref.putBool("fAuto", cfg.focusAutoMode);
  pref.putBool("fMan", cfg.focusManualMode);

  // Streak Settings
  pref.putUChar("sks", (uint8_t)cfg.streakSource);
  pref.putUChar("dpc", cfg.dailyPomodoroCount);
  pref.putUChar("ghc", cfg.githubContribCount);

  // Firebase Cloud Sync
  pref.putString("fbUrl", cfg.firebaseUrl);
  pref.putString("fbKey", cfg.firebaseSecret);
  pref.putBool("fbOn", cfg.firebaseEnabled);

  // Weather Widget (Open-Meteo)
  pref.putString("wCity", cfg.weatherCity);
  pref.putBool("wCel", cfg.weatherCelsius);
  pref.putFloat("wLat", cfg.weatherLat);
  pref.putFloat("wLon", cfg.weatherLon);

  // Spotify Integration
  pref.putString("spId", cfg.spotifyClientId);
  pref.putString("spSec", cfg.spotifySecret);
  pref.putString("spTok", cfg.spotifyToken);
  pref.putString("spRef", cfg.spotifyRefresh);

  // LLM Agent
  pref.putString("lKey", cfg.llmApiKey);
  pref.putUChar("lPrv", cfg.llmProvider);
  pref.putChar("lMod", cfg.llmModeOverride);

  for (int i = 0; i < 3; i++) {
    String key = "t" + String(i);
    pref.putString(key.c_str(), tasks[i]);
    String keyd = "d" + String(i);
    pref.putBool(keyd.c_str(), taskDone[i]);
    String keyDesc = "td" + String(i);
    pref.putString(keyDesc.c_str(), taskDescription[i]);
  }

  for (int i = 0; i < STREAK_N; i++) {
    String k = "s" + String(i);
    pref.putUChar(k.c_str(), streak[i]);
  }
  pref.putUChar("day", (uint8_t)currentDayIndex);

  pref.putUChar("ch", (uint8_t)clockH);
  pref.putUChar("cm", (uint8_t)clockM);
  pref.putUChar("cs", (uint8_t)clockS);
  pref.end();
}

void TaskyModel::startTimer() {
  st.running = true;
  st.finished = false;
  st.startMs = millis();
}

void TaskyModel::pauseTimer() {
  st.running = false;
  st.pausedMs = elapsedMs();
}

void TaskyModel::resetTimer() {
  st.running = false;
  st.finished = false;
  st.pausedMs = 0;
  st.startMs = millis();
}

void TaskyModel::finishTimer() {
  st.running = false;
  st.finished = true;
  st.pausedMs = phaseDurationMs();

  if (phase == PHASE_WORK) {
    cfg.totalFocusMin += cfg.workMin;
    cfg.dailyPomodoroCount++;  // Track daily sessions
    updateStreakIntensity();    // Update streak based on source
    save();
  }
}

void TaskyModel::nextPhase() {
  phase = (phase == PHASE_WORK) ? PHASE_BREAK : PHASE_WORK;
  resetTimer();
}

uint32_t TaskyModel::phaseDurationMs() {
  uint8_t m = (phase == PHASE_WORK) ? cfg.workMin : cfg.breakMin;
  return (uint32_t)m * 60UL * 1000UL;
}

uint32_t TaskyModel::elapsedMs() {
  if (!st.running)
    return st.pausedMs;
  return st.pausedMs + (millis() - st.startMs);
}

uint32_t TaskyModel::remainingMs() {
  uint32_t dur = phaseDurationMs();
  uint32_t el = elapsedMs();
  return (el >= dur) ? 0 : (dur - el);
}

void TaskyModel::streakAddCompletion() {
  if (streak[currentDayIndex] < 3)
    streak[currentDayIndex]++;
  save();
}

void TaskyModel::getClockNow(int &h, int &m, int &s) {
  uint32_t deltaMs = (millis() - clockSetAtMs);
  uint32_t deltaSec = deltaMs / 1000UL;

  int total = clockH * 3600 + clockM * 60 + clockS + (int)deltaSec;
  total %= (24 * 3600);
  if (total < 0)
    total += 24 * 3600;

  h = total / 3600;
  int rem = total % 3600;
  m = rem / 60;
  s = rem % 60;
}

int TaskyModel::calculateCurrentStreak() {
  // Count consecutive days with activity (value > 0), going backwards from current day
  // Supports wrap-around: if we reach index 0, we continue from STREAK_N-1
  int count = 0;
  int idx = currentDayIndex;
  int checked = 0;

  // Check consecutive days with activity (wrap around the array)
  while (checked < STREAK_N && streak[idx] > 0) {
    count++;
    checked++;
    idx--;
    if (idx < 0) idx = STREAK_N - 1;  // Wrap around
  }

  // Update best streak if current is better
  if (count > (int)cfg.bestStreak) {
    cfg.bestStreak = count;
    // Note: save() should be called by caller to persist
  }

  return count;
}

int TaskyModel::calculateTotalActiveDays() {
  // Count total days with any activity (green squares)
  int total = 0;
  for (int i = 0; i < STREAK_N; i++) {
    if (streak[i] > 0) total++;
  }
  return total;
}

void TaskyModel::checkDayChange() {
  // Non-blocking time check using time() instead of getLocalTime()
  time_t nowTime = time(nullptr);

  // Check if time is valid (after year 2020 means NTP synced)
  if (nowTime < 1577836800) {
    // No valid time yet - NTP hasn't synced
    return;
  }

  struct tm now;
  localtime_r(&nowTime, &now);  // Non-blocking conversion

  int today = now.tm_yday;  // Day of year (0-365)

  // First call - just store the day
  if (lastKnownDay == -1) {
    lastKnownDay = today;
    return;
  }

  // Check if day changed
  if (today != lastKnownDay) {
    // Day changed! Advance streak index
    currentDayIndex = (currentDayIndex + 1) % STREAK_N;
    streak[currentDayIndex] = 0;  // Reset new day to 0

    // Reset daily counters
    cfg.dailyPomodoroCount = 0;
    cfg.githubContribCount = 0;
    cfg.llmDailyCount = 0;
    cfg.llmHourlyCount = 0;

    // Clear calendar events for refresh
    for (int i = 0; i < 31; i++) {
      calendarEvents[i] = 0;
    }

    // Clear today's events and month events
    clearTodayEvents();
    clearMonthEvents();

    lastKnownDay = today;
    save();

    Serial.println("[Model] Day changed, streak index advanced to " + String(currentDayIndex));
  }
}

bool TaskyModel::isFocusModeActive() {
  // Manual override always wins
  if (cfg.focusManualMode) return true;

  // Auto mode: active during work phase when timer running
  if (cfg.focusAutoMode && st.running && phase == PHASE_WORK) {
    return true;
  }

  return false;
}

void TaskyModel::updateStreakIntensity() {
  // Calculate Tasky intensity based on Pomodoro sessions
  uint8_t taskyIntensity = 0;
  if (cfg.dailyPomodoroCount >= 5) taskyIntensity = 3;
  else if (cfg.dailyPomodoroCount >= 3) taskyIntensity = 2;
  else if (cfg.dailyPomodoroCount >= 1) taskyIntensity = 1;

  // Calculate GitHub intensity based on contributions
  uint8_t githubIntensity = 0;
  if (cfg.githubContribCount >= 8) githubIntensity = 3;
  else if (cfg.githubContribCount >= 4) githubIntensity = 2;
  else if (cfg.githubContribCount >= 1) githubIntensity = 1;

  // Apply based on source setting
  switch (cfg.streakSource) {
    case STREAK_GITHUB:
      streak[currentDayIndex] = githubIntensity;
      break;
    case STREAK_TASKY:
      streak[currentDayIndex] = taskyIntensity;
      break;
    case STREAK_BOTH:
      streak[currentDayIndex] = max(githubIntensity, taskyIntensity);
      break;
  }
}

void TaskyModel::clearTodayEvents() {
  todayEventCount = 0;
  memset(todayEvents, 0, sizeof(todayEvents));
}

void TaskyModel::clearMonthEvents() {
  monthEventCount = 0;
  memset(monthEvents, 0, sizeof(monthEvents));
}
