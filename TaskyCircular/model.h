#pragma once

#include <Arduino.h>
#include <Preferences.h>

// -------------------- Constants --------------------
#define STREAK_ROWS 7
#define STREAK_COLS 16
#define STREAK_N (STREAK_ROWS * STREAK_COLS)

enum Phase { PHASE_WORK, PHASE_BREAK };

struct Config {
  uint8_t workMin = 25;
  uint8_t breakMin = 5;
  uint8_t screenBright = 200;
  uint8_t ledBright = 180;
  bool soundOn = true;
  bool darkMode = true;
};

struct TimerState {
  bool running = false;
  bool finished = false;
  uint32_t startMs = 0;
  uint32_t pausedMs = 0;
};

class TaskyModel {
public:
  Config cfg;
  TimerState st;
  Phase phase = PHASE_WORK;
  Preferences pref;

  // Tasks
  String tasks[3];
  bool taskDone[3];

  // Streak
  uint8_t streak[STREAK_N];
  int currentDayIndex = 0;
  int streakCursor = 0; // For UI navigation

  // Clock
  int clockH = 12, clockM = 0, clockS = 0;
  uint32_t clockSetAtMs = 0;

  void begin();
  void load();
  void save();

  // Logic
  void startTimer();
  void pauseTimer();
  void resetTimer();
  void finishTimer();
  void nextPhase();

  uint32_t phaseDurationMs();
  uint32_t elapsedMs();
  uint32_t remainingMs();

  void streakAddCompletion();
  void getClockNow(int &h, int &m, int &s);
  uint8_t clampU8(int v, int lo, int hi);

private:
};
