#include "model.h"
#include "config_features.h"
#include <time.h>  // For getLocalTime()

void TaskyModel::begin() {
  load();
  applyTimezone();
  if (needsSave)  // a one-time migration happened in load()
    save();
  // Every device gets its own setup-network password, shown on screen and in
  // the QR code, so a password published in the repo can't open any Tasky.
  if (cfg.apPass.length() < 8) {
    const char *alphabet = "abcdefghjkmnpqrstuvwxyz23456789";  // no 0/o/1/l/i
    cfg.apPass = "";
    for (int i = 0; i < 10; i++)
      cfg.apPass += alphabet[esp_random() % 31];
    save();
  }
}

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
  cfg.ledType = pref.getUChar("ledT", 0);
  cfg.screens = pref.getString("scr", "");
  cfg.startScreen = pref.getString("start", "home");
  cfg.phrases = pref.getString("phr", "");
  cfg.hostOs = pref.getUChar("os", 0);
  cfg.btOn = pref.getBool("bt", false);
  cfg.agentsSeen = pref.getBool("agS", false);
  cfg.soundOn = pref.getBool("snd", true);
  cfg.darkMode = pref.getBool("dark", true);
  cfg.totalFocusMin = pref.getUInt("focus", 0);
  cfg.bestStreak = pref.getUShort("best", 0);
  cfg.wifiSSID = pref.getString("wss", "");
  cfg.wifiPass = pref.getString("wps", "");
  cfg.apPass = pref.getString("app", "");
  cfg.timezone = pref.getChar("tz", -5);
  cfg.githubUser = pref.getString("ghu", "");
  cfg.githubToken = pref.getString("ght", "");
  cfg.calendarUrl = pref.getString("cal", "");

  // Notion Integration
  cfg.notionApiKey = pref.getString("nkey", "");
  cfg.notionDatabaseId = pref.getString("ndb", "");

  // Focus Mode
  cfg.focusAutoMode = pref.getBool("fAuto", true);
  cfg.focusLight = pref.getBool("fLit", true);
  cfg.focusLock = pref.getBool("fLck", true);

  // Workday
  cfg.dayStartHour = pref.getUChar("dsh", 9);
  cfg.dayEndHour = pref.getUChar("deh", 18);
  cfg.lastMorningDay = pref.getShort("bmd", -1);
  cfg.lastEveningDay = pref.getShort("bed", -1);

  // Streak Settings
  cfg.streakSource = (StreakSource)pref.getUChar("sks", STREAK_BOTH);
  cfg.dailyPomodoroCount = pref.getUChar("dpc", 0);
  cfg.dailyActivity = pref.getUShort("dac", 0);
  cfg.lastDayNum = pref.getInt("lday", 0);
  cfg.yesterdayPomodoro = pref.getUChar("ypc", 0);
  cfg.dailyFocusMin = pref.getUShort("dfm", 0);
  cfg.dailyGoalMin = pref.getUShort("goal", 120);
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
    String keyFocus = "tf" + String(i);
    taskFocusMin[i] = pref.getUShort(keyFocus.c_str(), 0);
  }
  currentTask = pref.getChar("ct", -1);

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
  memset(ghStreak, 0, sizeof(ghStreak));
  if (pref.isKey("ghs"))
    pref.getBytes("ghs", ghStreak, sizeof(ghStreak));
  // 2.12 and older kept GitHub and Tasky days mixed in one grid. Your grid is
  // Tasky-only now, so a mixed one starts over (GitHub's refills from the API).
  streakMigrated = pref.getBool("smig", false);
  currentDayIndex = (int)pref.getUChar("day", 0);
  if (currentDayIndex < 0 || currentDayIndex >= STREAK_N)
    currentDayIndex = 0;

  clockH = (int)pref.getUChar("ch", 12);
  clockM = (int)pref.getUChar("cm", 0);
  clockS = (int)pref.getUChar("cs", 0);
  pref.end();

  if (!streakMigrated) {
    if (cfg.streakSource != STREAK_TASKY) {
      memset(streak, 0, sizeof(streak));
      cfg.bestStreak = 0;
    }
    streakMigrated = true;
    needsSave = true;
  }

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
  pref.putUChar("ledT", cfg.ledType);
  pref.putString("scr", cfg.screens);
  pref.putString("start", cfg.startScreen);
  pref.putString("phr", cfg.phrases);
  pref.putUChar("os", cfg.hostOs);
  pref.putBool("bt", cfg.btOn);
  pref.putBool("agS", cfg.agentsSeen);
  pref.putBool("snd", cfg.soundOn);
  pref.putBool("dark", cfg.darkMode);
  pref.putUInt("focus", cfg.totalFocusMin);
  pref.putUShort("best", cfg.bestStreak);
  pref.putString("wss", cfg.wifiSSID);
  pref.putString("wps", cfg.wifiPass);
  pref.putString("app", cfg.apPass);
  pref.putChar("tz", cfg.timezone);
  pref.putString("ghu", cfg.githubUser);
  pref.putString("ght", cfg.githubToken);
  pref.putString("cal", cfg.calendarUrl);

  // Notion Integration
  pref.putString("nkey", cfg.notionApiKey);
  pref.putString("ndb", cfg.notionDatabaseId);

  // Focus Mode
  pref.putBool("fAuto", cfg.focusAutoMode);
  pref.putBool("fLit", cfg.focusLight);
  pref.putBool("fLck", cfg.focusLock);

  // Workday
  pref.putUChar("dsh", cfg.dayStartHour);
  pref.putUChar("deh", cfg.dayEndHour);
  pref.putShort("bmd", cfg.lastMorningDay);
  pref.putShort("bed", cfg.lastEveningDay);

  // Streak Settings
  pref.putUChar("sks", (uint8_t)cfg.streakSource);
  pref.putUChar("dpc", cfg.dailyPomodoroCount);
  pref.putUShort("dac", cfg.dailyActivity);
  pref.putInt("lday", cfg.lastDayNum);
  pref.putUChar("ypc", cfg.yesterdayPomodoro);
  pref.putUShort("dfm", cfg.dailyFocusMin);
  pref.putUShort("goal", cfg.dailyGoalMin);
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
    String keyFocus = "tf" + String(i);
    pref.putUShort(keyFocus.c_str(), taskFocusMin[i]);
  }
  pref.putChar("ct", currentTask);

  for (int i = 0; i < STREAK_N; i++) {
    String k = "s" + String(i);
    pref.putUChar(k.c_str(), streak[i]);
  }

  pref.putBytes("ghs", ghStreak, sizeof(ghStreak));

  pref.putBool("smig", streakMigrated);
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
    cfg.dailyFocusMin += cfg.workMin;
    if (hasCurrentTask())
      taskFocusMin[currentTask] += cfg.workMin;
    cfg.dailyPomodoroCount++;  // Track daily sessions
    addActivity(3);             // greener streak square (saves)
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

// Using Tasky makes today's square greener: a finished focus session = 3,
// a task done = 2, an agent question answered = 1 (see updateStreakIntensity)
void TaskyModel::addActivity(uint8_t points) {
  cfg.dailyActivity += points;
  updateStreakIntensity();
  save();
}

void TaskyModel::streakAddCompletion() { addActivity(2); }

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

int TaskyModel::calculateCurrentStreak(bool github) {
  // Count consecutive days with activity (value > 0), going backwards from current day
  // Supports wrap-around: if we reach index 0, we continue from STREAK_N-1
  const uint8_t *grid = github ? ghStreak : streak;
  int count = 0;
  int idx = currentDayIndex;
  int checked = 0;

  // Check consecutive days with activity (wrap around the array)
  while (checked < STREAK_N && grid[idx] > 0) {
    count++;
    checked++;
    idx--;
    if (idx < 0) idx = STREAK_N - 1;  // Wrap around
  }

  // Update best streak if current is better (your own streak only)
  if (!github && count > (int)cfg.bestStreak) {
    cfg.bestStreak = count;
    // Note: save() should be called by caller to persist
  }

  return count;
}

int TaskyModel::calculateTotalActiveDays(bool github) {
  // Count total days with any activity (green squares)
  const uint8_t *grid = github ? ghStreak : streak;
  int total = 0;
  for (int i = 0; i < STREAK_N; i++) {
    if (grid[i] > 0) total++;
  }
  return total;
}

void TaskyModel::checkDayChange() {
  // Non-blocking time check using time() instead of getLocalTime()
  time_t nowTime = time(nullptr);
  if (nowTime < 1577836800)
    return;  // no valid time yet (NTP / browser clock)

  // Local day number, saved in NVS, so days that pass while Tasky is off
  // still move the streak on (and today's counters start from zero).
  int32_t today = (int32_t)((nowTime + (time_t)cfg.timezone * 3600) / 86400);
  if (cfg.lastDayNum == 0) {  // first run ever
    cfg.lastDayNum = today;
    save();
    return;
  }
  if (today <= cfg.lastDayNum)
    return;

  int days = min((int32_t)STREAK_N, today - cfg.lastDayNum);
  for (int d = 0; d < days; d++) {
    currentDayIndex = (currentDayIndex + 1) % STREAK_N;
    streak[currentDayIndex] = 0;  // a new (or missed) day starts empty
    ghStreak[currentDayIndex] = 0;
  }

  // Reset daily counters
  cfg.yesterdayPomodoro = days == 1 ? cfg.dailyPomodoroCount : 0;
  cfg.dailyPomodoroCount = 0;
  cfg.dailyActivity = 0;
  cfg.dailyFocusMin = 0;
  cfg.githubContribCount = 0;
  cfg.llmDailyCount = 0;
  cfg.llmHourlyCount = 0;

  // Clear calendar events for refresh
  for (int i = 0; i < 31; i++)
    calendarEvents[i] = 0;
  clearTodayEvents();
  clearMonthEvents();

  cfg.lastDayNum = today;
  save();
  Serial.println("[Model] New day (" + String(days) + "), streak index " + String(currentDayIndex));
}

bool TaskyModel::isFocusModeActive() {
  return cfg.focusAutoMode && st.running && phase == PHASE_WORK;
}

bool TaskyModel::hasCurrentTask() {
  return currentTask >= 0 && currentTask < 3 && tasks[currentTask] != "Empty" &&
         tasks[currentTask].length() > 0;
}

int TaskyModel::hhmmToMinutes(const char *hhmm) {
  if (!hhmm || strlen(hhmm) < 5 || hhmm[2] != ':' || !isdigit(hhmm[0]))
    return -1;
  return atoi(hhmm) * 60 + atoi(hhmm + 3);
}

const CalendarEventItem *TaskyModel::nextEventToday(int nowMin) {
  const CalendarEventItem *best = nullptr;
  int bestMin = 24 * 60;
  for (int i = 0; i < todayEventCount; i++) {
    const CalendarEventItem &e = todayEvents[i];
    int start = hhmmToMinutes(e.startTime);
    if (e.isAllDay || start < nowMin || start >= bestMin)
      continue;
    best = &e;
    bestMin = start;
  }
  return best;
}

const CalendarEventItem *TaskyModel::firstEventOnDay(int dayOfMonth) {
  const CalendarEventItem *best = nullptr;
  int bestMin = 24 * 60;
  for (int i = 0; i < monthEventCount; i++) {
    const CalendarEventItem &e = monthEvents[i];
    int start = hhmmToMinutes(e.startTime);
    if (e.day != dayOfMonth || e.isAllDay || start < 0 || start >= bestMin)
      continue;
    best = &e;
    bestMin = start;
  }
  return best;
}

void TaskyModel::applyTimezone() {
  char tz[12];
  snprintf(tz, sizeof(tz), "UTC%+d", -cfg.timezone);  // POSIX sign is inverted
  setenv("TZ", tz, 1);
  tzset();
}

void TaskyModel::setClock(time_t epochUtc) {
  if (epochUtc < 1577836800)  // reject anything before 2020
    return;
  applyTimezone();
  struct timeval tv = {epochUtc, 0};
  settimeofday(&tv, nullptr);
  struct tm t;
  localtime_r(&epochUtc, &t);
  clockH = t.tm_hour;
  clockM = t.tm_min;
  clockS = t.tm_sec;
  clockSetAtMs = millis();
  Serial.printf("[Clock] Set from browser: %02d:%02d\n", t.tm_hour, t.tm_min);
}

bool TaskyModel::localNow(struct tm &out) {
  time_t nowTime = time(nullptr);
  if (nowTime < 1577836800) // Before 2020 = NTP not synced yet
    return false;
  localtime_r(&nowTime, &out);
  return true;
}

void TaskyModel::updateStreakIntensity() {
  // Tasky intensity from today's activity points (addActivity)
  uint8_t taskyIntensity = 0;
  if (cfg.dailyActivity >= 8) taskyIntensity = 3;
  else if (cfg.dailyActivity >= 4) taskyIntensity = 2;
  else if (cfg.dailyActivity >= 1) taskyIntensity = 1;

  // Calculate GitHub intensity based on contributions
  uint8_t githubIntensity = 0;
  if (cfg.githubContribCount >= 8) githubIntensity = 3;
  else if (cfg.githubContribCount >= 4) githubIntensity = 2;
  else if (cfg.githubContribCount >= 1) githubIntensity = 1;

  // Two grids: yours (Tasky activity) and GitHub's
  streak[currentDayIndex] = taskyIntensity;
  ghStreak[currentDayIndex] = githubIntensity;
}

void TaskyModel::clearTodayEvents() {
  todayEventCount = 0;
  memset(todayEvents, 0, sizeof(todayEvents));
}

void TaskyModel::clearMonthEvents() {
  monthEventCount = 0;
  memset(monthEvents, 0, sizeof(monthEvents));
}
