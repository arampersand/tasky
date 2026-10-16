#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

// -------------------- Constants --------------------
#define STREAK_ROWS 6   // Reduced for circular display
#define STREAK_COLS 14  // Reduced for circular display corners
#define STREAK_N (STREAK_ROWS * STREAK_COLS)

#define MAX_TODAY_EVENTS 5
#define MAX_MONTH_EVENTS 20
#define MAX_NOTION_TASKS 5

enum Phase { PHASE_WORK, PHASE_BREAK };

// Streak source selection
enum StreakSource {
  STREAK_GITHUB = 0,  // GitHub contributions only
  STREAK_TASKY = 1,   // Tasky Pomodoro sessions only
  STREAK_BOTH = 2     // Combined (maximum of both)
};

// Calendar event for display (today's events)
struct CalendarEventItem {
  char title[33];       // SUMMARY (32 chars + null)
  char startTime[6];    // HH:MM + null
  char endTime[6];      // HH:MM + null
  char description[65]; // DESCRIPTION (64 chars + null)
  uint8_t day;          // Day of month (1-31)
  bool isAllDay;        // All-day event flag
};

// Notion task item
struct NotionTaskItem {
  char title[33];       // Task name (32 chars + null)
  char description[65]; // Description (64 chars + null)
  bool done;            // Checkbox status
  char dueDate[11];     // YYYY-MM-DD + null (optional)
};

struct Config {
  uint8_t workMin = 25;
  uint8_t breakMin = 5;
  uint8_t screenBright = 200;
  uint8_t ledBright = 180;
  uint8_t ledType = 0;  // 0 = PWM LED(s), 1 = SK6812 RGB chain (see config_hardware.h)
  String screens = "";  // MODE carousel ids, "home,timer,..." ("" = default)
  String phrases = "";  // Your phrases, one per line ("" = built-in)
  uint8_t hostOs = 0;   // Controls shortcuts: 0 = Mac, 1 = Windows
  bool soundOn = true;
  bool darkMode = true;
  uint32_t totalFocusMin = 0;
  uint16_t bestStreak = 0;  // Best consecutive days with activity
  String wifiSSID = "";
  String wifiPass = "";
  String apPass = "";   // Per-device setup-network password (generated on first boot)
  int8_t timezone = -5; // UTC Offset
  String githubUser = "";
  String githubToken = "";
  String calendarUrl = "";

  // Notion Integration
  String notionApiKey = "";
  String notionDatabaseId = "";

  // Focus Mode Settings (active while a work session runs)
  bool focusAutoMode = true;      // Enter focus when the work timer runs
  bool focusLight = true;         // Light bar on as "do not disturb" signal
  bool focusLock = true;          // MODO can't leave the timer while focusing

  // Workday (local hours) for morning brief / day close
  uint8_t dayStartHour = 9;
  uint8_t dayEndHour = 18;
  int16_t lastMorningDay = -1;      // tm_yday the morning brief was shown
  int16_t lastEveningDay = -1;      // tm_yday the day close was shown

  // Streak Settings
  StreakSource streakSource = STREAK_BOTH;
  uint8_t dailyPomodoroCount = 0;   // Pomodoro sessions completed today
  uint16_t dailyActivity = 0;       // Today's activity points (streak greens)
  int32_t lastDayNum = 0;           // Local day number last seen (0 = never)
  uint8_t yesterdayPomodoro = 0;    // Sessions completed yesterday
  uint16_t dailyFocusMin = 0;       // Focus minutes today
  uint8_t githubContribCount = 0;   // GitHub contributions today

  // Firebase Cloud Sync
  String firebaseUrl = "";          // project-id.firebaseio.com
  String firebaseSecret = "";       // Database secret or API key
  bool firebaseEnabled = false;

  // Weather Widget (Open-Meteo - no API key needed)
  String weatherCity = "";          // City name (e.g., "Mexico City")
  bool weatherCelsius = true;       // true = Celsius, false = Fahrenheit
  float weatherLat = 0;             // Latitude (from geocoding or manual)
  float weatherLon = 0;             // Longitude

  // Spotify Integration
  String spotifyClientId = "";      // Spotify App Client ID
  String spotifySecret = "";        // Spotify App Client Secret
  String spotifyToken = "";         // Access token (refreshable)
  String spotifyRefresh = "";       // Refresh token

  // LLM Agent
  String llmApiKey = "";            // API key (Anthropic or OpenAI)
  uint8_t llmProvider = 0;          // 0=Anthropic, 1=OpenAI
  int8_t llmModeOverride = -1;      // -1=auto (daily rotation), 0-6=manual mode
  uint16_t llmDailyCount = 0;       // Requests today (for rate limiting)
  uint16_t llmHourlyCount = 0;      // Requests this hour
  uint32_t llmHourStart = 0;        // millis() when hourly count reset

  // Runtime Data (not saved to config necessarily, or maybe yes for offline cache)
  String nextEventSummary = "No Events";
  String nextEventTime = "--:--";
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
  String taskDescription[3];  // Extended descriptions for long press view
  bool taskDone[3];
  uint16_t taskFocusMin[3];   // Focus minutes spent on each task
  int8_t currentTask = -1;    // Task the timer is working on (-1 = none)

  // Calendar events (for grid view)
  uint8_t calendarEvents[31];  // Events per day of month (0-3 intensity)

  // Today's calendar events (detailed, for agenda screen)
  CalendarEventItem todayEvents[MAX_TODAY_EVENTS];
  int todayEventCount = 0;

  // Month calendar events (for day detail on any day)
  CalendarEventItem monthEvents[MAX_MONTH_EVENTS];
  int monthEventCount = 0;

  // Notion tasks
  NotionTaskItem notionTasks[MAX_NOTION_TASKS];
  int notionTaskCount = 0;

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

  void streakAddCompletion();      // a task done
  void addActivity(uint8_t points);
  void getClockNow(int &h, int &m, int &s);
  uint8_t clampU8(int v, int lo, int hi);

  // Streak statistics
  int calculateCurrentStreak();    // Returns consecutive days with activity from current day
  int calculateTotalActiveDays();  // Returns total days with any activity (green squares)
  void checkDayChange();           // Check if day changed and advance streak index

  // Focus mode
  bool isFocusModeActive();        // True while a work session runs (if enabled)
  bool hasCurrentTask();           // currentTask points at a real task

  // Local time (false until NTP has synced)
  bool localNow(struct tm &out);
  // Time without internet: the setup page / phone portal sends the browser's
  // clock. NTP corrects it later when Wi-Fi is up.
  void applyTimezone();            // TZ env from cfg.timezone (whole hours)
  void setClock(time_t epochUtc);

  // Calendar helpers
  static int hhmmToMinutes(const char *hhmm);      // "HH:MM" -> minutes, -1 if invalid
  const CalendarEventItem *nextEventToday(int nowMin); // First timed event at/after nowMin
  const CalendarEventItem *firstEventOnDay(int dayOfMonth);

  // Streak intensity
  void updateStreakIntensity();    // Update streak based on source setting

  // Clear today's events (for refresh)
  void clearTodayEvents();
  void clearMonthEvents();

};
