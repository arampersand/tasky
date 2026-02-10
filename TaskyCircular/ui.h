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
  SCR_TIMER = 0,
  SCR_FOCUS,      // Immersive focus mode
  SCR_TASKS,
  SCR_AGENDA,     // Today's calendar + Notion tasks
  SCR_CALENDAR,
  SCR_STREAK,
  SCR_STATS,
  SCR_WEATHER,    // Weather widget screen
  SCR_SPOTIFY,    // Spotify now playing + controls
  SCR_SETTINGS,
  SCR_QR,
  SCR_WIFI_INFO,
  SCR_QUOTES      // Inspirational quotes (screensaver mode)
};

class TaskyUI {
public:
  uint16_t C_BG, C_TEXT, C_MUTED, C_LINE, C_SOFT;
  uint16_t C_TEAL = 0x2E57;
  uint16_t C_GREEN = 0x07E0;
  uint16_t C_ORANGE = 0xFD20;
  uint16_t C_RED = 0xF800;
  uint16_t C_SPLASH_BG, C_SPLASH_TX;

  bool flashOn = false;
  uint32_t flashUntilMs = 0;
  bool baseDrawn = false;

  // Animation system
  AnimState anim;

  int lastShownMin = -1;
  int lastShownSec = -1;
  int lastProgSegOn = -1;

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

  // Quotes / Screensaver
  int quoteIndex = 0;
  uint32_t lastQuoteChange = 0;

  // Use correct circular driver
  // ...

  // ...

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
  void drawSettings();
  void drawTasks();
  void drawTaskDetail(int index);
  void drawCalendar();
  void drawDayTasks(int day); // Show tasks for a specific calendar day
  void drawFocus();           // Immersive focus mode screen
  void drawAgenda();          // Today's calendar events + Notion tasks
  void drawFocusIndicator(bool active);  // Shield icon when blocking
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
  void drawWifi(bool wifiOn, String ssid, String pass, String ip);
  void drawQRFull(String ssid, String pass);
  void drawSplashTasky();
  void drawFinishScreen(const char *msg);
  void drawQuotes(); // Inspirational quotes screen

  void uiTick(bool strong = false); // kept for visual reference, sound handled
                                    // externally if desired or inside
  void drawFlashIfNeeded();

  void applyTheme();

  void drawFrame(); // Implementation of the required abstraction

  void drawNavDots(Screen current); // Navigation indicator dots

private:
  void drawProgressSegments();
  uint16_t accentColor();
  uint16_t streakColor(uint8_t v);
  uint16_t calendarDayColor(uint8_t eventCount);  // Color based on busyness

  void centerTextFont(const char *s, int baselineY, uint16_t color,
                      const GFXfont *font);
  void rightTextFont(const String &s, int xRight, int baselineY, uint16_t color,
                     const GFXfont *font);
  int8_t _rst; // Store reset pin
};
