#pragma once

#include "model.h"
#include "animation.h"
#include <Adafruit_GFX.h>
#include <Arduino.h>

// --- Compile time switch ---
#define DISPLAY_CIRCULAR 0  // 0 = ST7789 square, 1 = GC9A01A circular
// ---------------------------

#if DISPLAY_CIRCULAR
#include <Adafruit_GC9A01A.h> // Driver A (Typical)
// #include <Adafruit_ST7789.h> // Driver B (Common fallback)
#else
#include <Adafruit_ST7789.h>
#endif

#include "qrcode.h"
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

enum Screen {
  SCR_TIMER = 0,
  SCR_QR,
  SCR_TASKS,
  SCR_CALENDAR,
  SCR_STREAK,
  SCR_STATS,
  SCR_SETTINGS,
  SCR_WIFI_INFO,
  SCR_QUOTES  // Inspirational quotes (screensaver mode)
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
  bool timerEditing = false;  // For editing timer duration

  // Calendar navigation
  int calendarSelDay = 0;  // 0 = no selection, 1-31 = selected day
  bool calendarLoading = false;  // Show loading indicator

  // Quotes / Screensaver
  int quoteIndex = 0;
  uint32_t lastQuoteChange = 0;

  // Use correct circular driver
#if DISPLAY_CIRCULAR
  Adafruit_GC9A01A tft;
#else
  Adafruit_ST7789 tft;
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
  void drawCalendar(); // [NEW]
  void drawStreak();
  void drawStats();
  void drawWifi(bool wifiOn, String ssid, String pass, String ip);
  void drawQRFull(String ssid, String pass);
  void drawSplashTasky();
  void drawFinishScreen(const char *msg);
  void drawQuotes();  // Inspirational quotes screen

  void uiTick(bool strong = false); // kept for visual reference, sound handled
                                    // externally if desired or inside
  void drawFlashIfNeeded();

  void applyTheme();

  void drawFrame(); // Implementation of the required abstraction

  void drawNavDots(Screen current);  // Navigation indicator dots

private:
  void drawProgressSegments();
  uint16_t accentColor();
  uint16_t streakColor(uint8_t v);

  void centerTextFont(const char *s, int baselineY, uint16_t color,
                      const GFXfont *font);
  void rightTextFont(const String &s, int xRight, int baselineY, uint16_t color,
                     const GFXfont *font);
  int8_t _rst; // Store reset pin
};
