#pragma once

#include "animation.h"
#include "config_features.h"
#include "model.h"
#include <Adafruit_GFX.h>
#include <Arduino.h>

// --- Compile time switch ---
#define DISPLAY_CIRCULAR 0 // 0 = ST7789 square, 1 = GC9A01A circular
// ---------------------------

#if DISPLAY_CIRCULAR
#include <Adafruit_GC9A01A.h> // Driver A (Typical)
// #include <Adafruit_ST7789.h> // Driver B (Common fallback)
#else
#include <Adafruit_ST7789.h>
#endif

#if FEATURE_QR_CODE
#include "qrcode.h"
#endif
#include "agents.h"
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
// Subclass to expose protected methods if needed
#if DISPLAY_CIRCULAR
// ...
#else
class TaskyST7789 : public Adafruit_ST7789 {
public:
  TaskyST7789(int8_t cs, int8_t dc, int8_t rst)
      : Adafruit_ST7789(cs, dc, rst) {}
  TaskyST7789(int8_t cs, int8_t dc, int8_t mos, int8_t sck, int8_t rst)
      : Adafruit_ST7789(cs, dc, mos, sck, rst) {}

  // Expose protected setColRowStart
  void setOffsets(int8_t col, int8_t row) { setColRowStart(col, row); }
};
#endif

enum Screen {
  // Main carousel (MODO button)
  SCR_TIMER = 0,
  SCR_TASKS,
  SCR_AGENDA,     // Today's calendar + Notion tasks
  SCR_STATS,
  SCR_MORE,       // Menu with the less frequent screens
  // Opened from More
  SCR_FOCUS,      // Focus mode settings
  SCR_CALENDAR,
  SCR_STREAK,
  SCR_WEATHER,    // Weather widget screen
  SCR_SPOTIFY,    // Spotify now playing + controls
  SCR_AGENT,      // AI Agent - LLM assistant
  SCR_UPDATE,     // Allow an OTA upload from the device
  SCR_SETTINGS,
  SCR_QR,
  SCR_WIFI_INFO,
  SCR_HOME,       // Glance: clock, weather, next event, tasks, phrase
  SCR_AGENTS,     // Agents edition: Claude Code sessions
  SCR_CONTROLS,   // Agents edition: Bluetooth volume / media / shortcuts
  SCR_TOUR,       // First-boot tour (also More -> Tour)
  // Overlays
  SCR_AMBIENT,    // Idle: clock + next event + weather
  SCR_NOTICE      // Meeting alert / morning brief / day close
};

enum NoticeType { NOTICE_MEETING, NOTICE_MORNING, NOTICE_EVENING };

class TaskyUI {
public:
  uint16_t C_BG, C_TEXT, C_MUTED, C_LINE, C_SOFT;
  uint16_t C_TEAL = 0x2E57;
  uint16_t C_GREEN = 0x07E0;
  uint16_t C_ORANGE = 0xFD20;
  uint16_t C_PURPLE = 0x780F;
  uint16_t C_RED = 0xF800;
  uint16_t C_SPLASH_BG, C_SPLASH_TX;

  bool baseDrawn = false;

  // Animation system
  AnimState anim;

  int lastShownMin = -1;
  int lastShownSec = -1;
  int lastProgOn = -1;          // Lit perimeter steps last drawn
  uint16_t lastProgColor = 0;   // Perimeter color last drawn
  int lastFocusShown = -1;      // Focus label state last drawn

  int settingsSel = 0;
  bool settingsEditing = false;
  int tasksSel = 0;
  bool streakEditing = false;
  bool clockEditing = false;
  int clockField = 0;
  bool timerEditing = false; // For editing timer duration

  // Calendar navigation
  int calendarSelDay = 0;       // 0 = no selection, 1-31 = selected day
  bool calendarLoading = false; // Show loading indicator
  bool showingDayTasks = false; // Showing day's tasks overlay

  // Agent screen
  bool agentShowingResponse = false;  // Showing LLM response
  int agentScrollOffset = 0;   // Scroll offset for long responses
  bool agentModeSelect = false;  // Mode selection overlay active
  int agentModeSel = 0;          // Selected index (0=AUTO, 1-7=modes)

  // Focus settings / More menu selections
  int focusSel = 0;
  int moreSel = 0;
  // Partial redraws: what the current list screen last painted
  int lastListSel = -1;
  int lastListStart = -1;

  // WiFi state (set from main loop)
  bool wifiConnected = false;

#if DISPLAY_CIRCULAR
  Adafruit_GC9A01A tft;
#else
  TaskyST7789 tft;
#endif

  TaskyModel *model;

  TaskyUI(TaskyModel *m, int8_t cs, int8_t dc, int8_t rst);

  void init();
  void goToScreen(Screen s);

  void drawTimerBase();
  void drawTimerTime(int mm, int ss);
  void drawTimerTaskLine(const char *overrideMsg = nullptr); // nullptr = task
  void drawSettings();
  void drawTasks();
  void drawTaskDetail(int index);
  void drawCalendar();
  void drawDayTasks(int day); // Show tasks for a specific calendar day
  void drawFocus();           // Focus mode settings
  void drawMore(const char *const labels[], int count);
  void drawAgenda();          // Today's calendar events + Notion tasks
  void drawAmbient(bool weatherOk, float temp, const char *desc, bool celsius);
  void drawAmbientClock();    // Only the clock digits (called each minute)
  void drawAmbientPhrase();   // Only the rotating phrase line
  // Replaces the phrase on Home when set (e.g. "1 agent waiting")
  String statusLine;
  uint16_t statusColor = 0;
#if FEATURE_AGENTS
  void drawAgents(const AgentBoard &board, int sel);
#endif
#if FEATURE_BLE_HID
  void drawControls(const char *mode, const char *value, const char *hint, bool connected);
#endif
  void drawMeetingAlert(const CalendarEventItem &ev, int minutes, bool timerPaused);
  void drawMorningBrief();
  void drawDayClose();
  void drawUpdate(bool armed, const String &url, const String &hint,
                  const String &version);
  void drawUpdateCountdown(uint32_t remainingMs);
  void drawStreak();
  void drawStats();
#if FEATURE_WEATHER
  void drawWeather(float temp, float feelsLike, uint8_t humidity,
                   uint8_t icon, const char* desc, const char* city, bool celsius);
#endif
#if FEATURE_SPOTIFY
  void drawSpotify(const char* track, const char* artist, uint32_t progressMs,
                   uint32_t durationMs, bool isPlaying, int volume, bool isActive);
#endif
#if FEATURE_LLM
  void drawAgent(int state, const char* response, int responseLen,
                 const char* errorMsg, const char* modeName, const char* modeDesc,
                 bool showingResponse, int scrollOffset);
  void drawAgentModeSelect(int selection, int8_t currentOverride);
#endif
  void drawWifi(bool wifiOn, String ssid, String pass, String ip);
  void drawQRFull(String ssid, String pass);
  void drawSplashTasky();
  void drawTour(int step);           // 0 turn, 1 press, 2 MODE, 3 done
  void drawHint(const char *text);   // One-line pill at the bottom
  void drawFinishScreen(const char *msg);

  void uiTick(bool strong = false); // Click beep + click animation

  void applyTheme();

private:
  void drawProgressPerimeter(bool force);
  void drawTimerLabel();
  void drawSessionDots(int cy);
  void drawRow(const char *label, const String &value, int baseline,
               uint16_t valueColor);
  String fitText(const String &s, int maxW, const GFXfont *font);
  uint16_t accentColor();
  uint16_t streakColor(uint8_t v);
  uint16_t calendarDayColor(uint8_t eventCount);  // Color based on busyness

  void centerTextFont(const char *s, int baselineY, uint16_t color,
                      const GFXfont *font);
  void rightTextFont(const String &s, int xRight, int baselineY, uint16_t color,
                     const GFXfont *font);
  int8_t _rst; // Store reset pin
};
