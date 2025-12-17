#include "model.h"

#include "model.h"

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
  cfg.wifiSSID = pref.getString("wss", "");
  cfg.wifiSSID = pref.getString("wss", "");
  cfg.wifiPass = pref.getString("wps", "");
  cfg.timezone = pref.getChar("tz", -5);
  cfg.githubUser = pref.getString("ghu", "");
  cfg.githubToken = pref.getString("ght", "");
  cfg.calendarUrl = pref.getString("cal", "");

  for (int i = 0; i < 3; i++) {
    String key = "t" + String(i);
    tasks[i] = pref.getString(key.c_str(), "Empty");
    String keyd = "d" + String(i);
    taskDone[i] = pref.getBool(keyd.c_str(), false);
  }

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
  pref.putString("wss", cfg.wifiSSID);
  pref.putString("wss", cfg.wifiSSID);
  pref.putString("wps", cfg.wifiPass);
  pref.putChar("tz", cfg.timezone);
  pref.putString("ghu", cfg.githubUser);
  pref.putString("ght", cfg.githubToken);
  pref.putString("cal", cfg.calendarUrl);

  for (int i = 0; i < 3; i++) {
    String key = "t" + String(i);
    pref.putString(key.c_str(), tasks[i]);
    String keyd = "d" + String(i);
    pref.putBool(keyd.c_str(), taskDone[i]);
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
