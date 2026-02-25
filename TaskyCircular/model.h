#pragma once

#include <Arduino.h>
#include <Preferences.h>

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
  bool soundOn = true;
  bool darkMode = true;
  uint32_t totalFocusMin = 0;
  uint16_t bestStreak = 0;  // Best consecutive days with activity
  String wifiSSID = "";
  String wifiPass = "";
  int8_t timezone = -5; // UTC Offset
  String githubUser = "";
  String githubToken = "";
  String calendarUrl = "";

  // Notion Integration
  String notionApiKey = "";
  String notionDatabaseId = "";

  // Focus Mode Settings
  bool focusAutoMode = true;      // Auto-enable during work sessions
  bool focusManualMode = false;   // Manual override (always block)

  // Streak Settings
  StreakSource streakSource = STREAK_BOTH;
  uint8_t dailyPomodoroCount = 0;   // Pomodoro sessions completed today
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

  void streakAddCompletion();
  void getClockNow(int &h, int &m, int &s);
  uint8_t clampU8(int v, int lo, int hi);

  // Streak statistics
  int calculateCurrentStreak();    // Returns consecutive days with activity from current day
  int calculateTotalActiveDays();  // Returns total days with any activity (green squares)
  void checkDayChange();           // Check if day changed and advance streak index

  // Focus mode
  bool isFocusModeActive();        // Returns true if DNS blocking should be active

  // Streak intensity
  void updateStreakIntensity();    // Update streak based on source setting

  // Clear today's events (for refresh)
  void clearTodayEvents();
  void clearMonthEvents();

private:
  int lastKnownDay = -1;  // For day change detection
};
