#include "ui.h"
#if FEATURE_AGENTS
#include "agents_sprites.h"
#endif
#include "config_hardware.h"
#include "config_features.h"
#include <time.h> // For getLocalTime()

// Buzzer constants
extern uint32_t lastUiBeepAt;
extern uint32_t uiBeepOffAt;
extern void buzzerTone(uint16_t hz);
extern void buzzerOff();

// --- Constants ---
static const int W = 240, H = 240, CX = 120, CY = 120;
// The case window hides ~1 mm (~9 px) of panel edge and varies per unit:
// keep every pixel of content inside [SAFE, W - SAFE].
static const int SAFE = 12;

// Timer progress: rounded-rect stroke that follows the case window.
// Centerline inset 14 px (stroke 12..16), corner radius 26.
static const int PERIM_L = 14, PERIM_T = 14, PERIM_R = 225, PERIM_B = 225;
static const int PERIM_RAD = 26;
static const int PERIM_STROKE = 4;
static const int PERIM_STEPS = 200; // ~4 px per step

TaskyUI::TaskyUI(TaskyModel *m, int8_t cs, int8_t dc, int8_t rst)
    : model(m), tft(cs, dc, rst), _rst(rst) {}

void TaskyUI::init() {
  // Reset secuencia completa para estabilidad
  pinMode(_rst, OUTPUT);
  digitalWrite(_rst, HIGH);
  delay(50);
  digitalWrite(_rst, LOW);
  delay(100);
  digitalWrite(_rst, HIGH);
  delay(250);  // Esperar a que el display este listo

#if DISPLAY_CIRCULAR
  tft.begin();
  tft.setRotation(0);
  tft.invertDisplay(true);
#else
  // ST7789 240x240 - init estable, SPI lento para evitar corrupcion
  tft.init(SCREEN_WIDTH, SCREEN_HEIGHT);
  tft.setSPISpeed(10000000);  // 10MHz - mas estable que 32MHz por defecto
  tft.setRotation(2);
  tft.invertDisplay(true);
  tft.setOffsets(COL_OFFSET, ROW_OFFSET);
  // Triple limpieza para eliminar artefactos en las primeras filas
  tft.fillScreen(0x0000);
  delay(30);
  tft.fillScreen(0x0000);
  delay(30);
  tft.fillScreen(0x0000);
  delay(30);
#endif

  applyTheme();
  clearScreen();
}

void TaskyUI::applyTheme() {
  Config &cfg = model->cfg;
  if (cfg.darkMode) {
    C_BG = 0x0000;
    C_TEXT = 0xFFFF;
    C_MUTED = 0x7BEF;
    C_LINE = 0x3186;
    C_SOFT = 0xE71C;
    C_SPLASH_BG = 0x0000;
    C_SPLASH_TX = C_TEAL;
  } else {
    C_BG = 0xFFFF;
    C_TEXT = 0x0000;
    C_MUTED = 0x7BEF;
    C_LINE = 0xC618;
    C_SOFT = 0x39E7;
    C_SPLASH_BG = 0xFFFF;
    C_SPLASH_TX = C_TEAL;
  }
}

void TaskyUI::goToScreen(Screen s) {
  // Just reset state - screen clearing handled by individual draw functions
  baseDrawn = false;
}

void TaskyUI::uiTick(bool strong) {
  uint32_t now = millis();
  if (model->cfg.soundOn && (now - lastUiBeepAt > 60)) {
    lastUiBeepAt = now;
    buzzerTone(strong ? 2600 : 2200);
    uiBeepOffAt = now + 10;
  }

  // Trigger click animation for visual feedback
  if (strong) {
    anim.triggerClick();
  }
}

uint16_t TaskyUI::accentColor() {
  return (model->phase == PHASE_WORK) ? C_TEAL : C_PURPLE;
}

// Helper to get color based on progress for ring
uint16_t getRingColor(float pct) {
  // pct 0.0 (empty) to 1.0 (full)
  if (pct > 0.66)
    return 0x2E57; // Teal/Blue-ish
  if (pct > 0.25)
    return 0x780F; // Purple
  return 0xF800;   // Red
}

// Point at step i of the rounded-rect centerline, starting at top center and
// going clockwise.
static void perimeterPoint(int i, int &x, int &y) {
  const float QUARTER_TURN = 1.5707963f;
  const float straight = (float)(PERIM_R - PERIM_L - 2 * PERIM_RAD);
  const float half = straight / 2;
  const float arc = QUARTER_TURN * PERIM_RAD;
  float s = (4 * straight + 4 * arc) * i / PERIM_STEPS;
  float fx, fy;

  if (s < half) {
    fx = (PERIM_L + PERIM_R) / 2.0f + s;
    fy = PERIM_T;
  } else {
    s -= half;
    // Corners TR, BR, BL, TL, each followed by the next side
    const float ccx[4] = {PERIM_R - PERIM_RAD, PERIM_R - PERIM_RAD,
                          PERIM_L + PERIM_RAD, PERIM_L + PERIM_RAD};
    const float ccy[4] = {PERIM_T + PERIM_RAD, PERIM_B - PERIM_RAD,
                          PERIM_B - PERIM_RAD, PERIM_T + PERIM_RAD};
    for (int c = 0; c < 4; c++) {
      if (s < arc) {
        float a = -QUARTER_TURN + c * QUARTER_TURN + s / PERIM_RAD;
        fx = ccx[c] + cosf(a) * PERIM_RAD;
        fy = ccy[c] + sinf(a) * PERIM_RAD;
        break;
      }
      s -= arc;
      if (c == 3 || s < straight) {
        switch (c) {
        case 0: fx = PERIM_R; fy = PERIM_T + PERIM_RAD + s; break;
        case 1: fx = PERIM_R - PERIM_RAD - s; fy = PERIM_B; break;
        case 2: fx = PERIM_L; fy = PERIM_B - PERIM_RAD - s; break;
        default: fx = PERIM_L + PERIM_RAD + s; fy = PERIM_T; break;
        }
        break;
      }
      s -= straight;
    }
  }
  x = (int)lroundf(fx);
  y = (int)lroundf(fy);
}

// The remaining time is lit; it ends at top center and the gap grows
// clockwise from top center as time passes. Only changed steps are redrawn.
void TaskyUI::drawProgressPerimeter(bool force) {
  uint32_t dur = model->phaseDurationMs();
  float pct = dur ? (float)model->remainingMs() / (float)dur : 0;
  int on = constrain((int)(pct * PERIM_STEPS + 0.5f), 0, PERIM_STEPS);
  uint16_t color = getRingColor(pct);

  if (lastProgOn < 0 || color != lastProgColor)
    force = true;
  if (!force && on == lastProgOn)
    return;

  int from = 0, to = PERIM_STEPS;
  if (!force) {
    from = PERIM_STEPS - max(on, lastProgOn);
    to = PERIM_STEPS - min(on, lastProgOn);
  }
  const int h = PERIM_STROKE / 2;
  for (int i = from; i < to; i++) {
    int x, y;
    perimeterPoint(i, x, y);
    tft.fillRect(x - h, y - h, PERIM_STROKE, PERIM_STROKE,
                 (i >= PERIM_STEPS - on) ? color : C_LINE);
    if ((i & 31) == 0)
      yield();
  }
  lastProgOn = on;
  lastProgColor = color;
}

void TaskyUI::drawTimerLabel() {
  bool focus = model->isFocusModeActive();
  const char *label = (model->phase == PHASE_WORK) ? "FOCUS" : "BREAK";

  tft.fillRect(36, 34, 168, 22, C_BG);
  tft.setFont(&FreeSans9pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  int dotSpace = focus ? 12 : 0;
  int x = (W - (int)w - dotSpace) / 2;
  if (focus)
    tft.fillCircle(x + 3, 44, 3, C_TEAL); // "focusing" dot
  tft.setTextColor(accentColor());
  tft.setCursor(x + dotSpace, 50);
  tft.print(label);
  tft.setFont();
  lastFocusShown = focus;
}

// Today's sessions as a 4-dot pomodoro cycle
void TaskyUI::drawSessionDots(int cy) {
  int n = model->cfg.dailyPomodoroCount;
  int filled = (n > 0 && n % 4 == 0) ? 4 : n % 4;
  tft.fillRect(CX - 32, cy - 6, 64, 13, C_BG);
  for (int i = 0; i < 4; i++) {
    int x = CX - 24 + i * 16;
    if (i < filled)
      tft.fillCircle(x, cy, 4, C_TEAL);
    else
      tft.drawCircle(x, cy, 4, C_LINE);
  }
}

void TaskyUI::drawTimerTaskLine(const char *overrideMsg) {
  tft.fillRect(24, 194, 192, 22, C_BG);
  if (overrideMsg) {
    centerTextFont(overrideMsg, 210, C_TEAL, &FreeSans9pt7b);
    return;
  }
  if (model->phase != PHASE_WORK || !model->hasCurrentTask())
    return;
  String t = fitText(model->tasks[model->currentTask], 180, &FreeSans9pt7b);
  centerTextFont(t.c_str(), 210, C_MUTED, &FreeSans9pt7b);
}

void TaskyUI::drawTimerBase() {
  clearScreen();
  baseDrawn = true;
  lastShownMin = -1;
  lastShownSec = -1;
  lastProgOn = -1;

  drawProgressPerimeter(true);
  drawTimerLabel();
  drawSessionDots(186);
  drawTimerTaskLine();
}

void TaskyUI::drawFinishScreen(const char *msg) {
  clearScreen();

  // Whole track lit green: the session is complete
  const int h = PERIM_STROKE / 2;
  for (int i = 0; i < PERIM_STEPS; i++) {
    int x, y;
    perimeterPoint(i, x, y);
    tft.fillRect(x - h, y - h, PERIM_STROKE, PERIM_STROKE, C_GREEN);
  }
  lastProgOn = -1; // Timer screen must repaint the track when it returns

  centerTextFont(msg, 118, C_TEXT, &FreeSansBold12pt7b);
  centerTextFont(model->phase == PHASE_WORK ? "Click: take a break"
                                            : "Click: back to focus",
                 150, C_MUTED, &FreeSans9pt7b);
  drawSessionDots(186);
}

void TaskyUI::drawTimerTime(int mm, int ss) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);

  tft.setFont(&FreeSansBold24pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  tft.fillRect(28, 96, 184, 48, C_BG);
  tft.setTextColor(C_TEXT);
  tft.setCursor((W - (int)w) / 2 - x1, 136);
  tft.print(buf);
  tft.setFont();

  // Status line, only while paused / done
  tft.fillRect(28, 148, 184, 24, C_BG);
  if (model->st.finished) {
    centerTextFont("DONE", 166, C_RED, &FreeSansBold12pt7b);
  } else if (!model->st.running) {
    centerTextFont("PAUSED", 166, C_MUTED, &FreeSansBold12pt7b);
  }

  if ((int)model->isFocusModeActive() != lastFocusShown)
    drawTimerLabel();
  drawProgressPerimeter(false);
}

void TaskyUI::drawSettings() {
  // Already on screen: repaint only the rows that changed (no full-screen flash)
  bool full = !baseDrawn;
  baseDrawn = true;
  if (full) {
    clearScreen();
    centerTextFont("SETTINGS", 32, C_TEXT, &FreeSansBold12pt7b);
    tft.drawFastHLine(30, 46, 180, C_LINE);
  }

  // Settings: Work, Break, WiFi, Bright, LED, Theme (0-5)
  const int itemCount = 6;
  const char *labels[6] = {"Work", "Break", "WiFi", "Bright", "LED", "Theme"};
  String values[6];
  values[0] = String(model->cfg.workMin) + "m";
  values[1] = String(model->cfg.breakMin) + "m";
  values[2] = model->cfg.wifiSSID.length() > 0 ? "ON" : "OFF";
  values[3] = String((model->cfg.screenBright * 100) / 255) + "%";
  values[4] = String((model->cfg.ledBright * 100) / 255) + "%";
  values[5] = model->cfg.darkMode ? "DARK" : "LIGHT";

  int yStart = 72;
  int rowH = 28;

  // Draw all items
  for (int i = 0; i < itemCount; i++) {
    int baseline = yStart + i * rowH;
    bool sel = (i == settingsSel);
    if (!full && !sel && i != lastListSel)
      continue;
    if (!full)  // rows are 28 px apart; 12pt caps reach baseline - 17
      tft.fillRect(20, baseline - 18, 200, 27, C_BG);

    // Draw highlight for selected item
    if (sel) {
      int w = 200;
      int x = (W - w) / 2;
      if (settingsEditing) {
        tft.drawRoundRect(x, baseline - 16, w, 24, 6, C_TEAL);
      } else {
        tft.drawRoundRect(x, baseline - 16, w, 24, 6, C_LINE);
      }
    }

    // Text color - always visible
    uint16_t labelColor = sel ? C_TEAL : C_TEXT;
    uint16_t valueColor = sel ? C_TEAL : C_MUTED;

    // WiFi row: color value green/red based on connection
    if (i == 2) {
      valueColor = wifiConnected ? C_GREEN : C_RED;
      if (sel) labelColor = C_TEAL;
    }

    tft.setTextColor(labelColor);
    tft.setFont(&FreeSans12pt7b); // Bigger font
    tft.setCursor(28, baseline);
    tft.print(labels[i]);
    if (i == 3 || i == 4) {  // Bright / LED: a level bar you can read at a glance
      int level = i == 3 ? model->cfg.screenBright : model->cfg.ledBright;
      const int bx = 128, bw = 80, by = baseline - 9;
      tft.fillRoundRect(bx, by, bw, 8, 4, C_LINE);
      int w = bw * level / 255;
      if (w > 0)
        tft.fillRoundRect(bx, by, max(8, w), 8, 4, sel ? C_TEAL : C_MUTED);
    } else {
      rightTextFont(values[i], 212, baseline, valueColor, &FreeSans12pt7b);
    }
  }
  lastListSel = settingsSel;
}

void TaskyUI::drawTasks() {
  // Check if all tasks are empty (no real content)
  bool allEmpty = true;
  for (int i = 0; i < 3; i++) {
    if (model->tasks[i].length() > 0 && model->tasks[i] != "Empty")
      allEmpty = false;
  }

  // Already on screen: repaint only the rows that changed
  bool full = !baseDrawn || allEmpty;
  baseDrawn = true;
  if (full) {
    clearScreen();
    centerTextFont("TASKS", 34, C_TEXT, &FreeSansBold12pt7b);
    tft.drawFastHLine(40, 46, 160, C_LINE);
  }

  if (allEmpty) {
    // Empty state - centered vertically
    centerTextFont("No tasks yet", 110, C_TEXT, &FreeSansBold12pt7b);
    centerTextFont("Scan the QR code", 140, C_MUTED, &FreeSans9pt7b);
    centerTextFont("to add your tasks", 160, C_MUTED, &FreeSans9pt7b);

    // QR icon hint - centered
    int qrX = 108, qrY = 175;
    tft.drawRect(qrX, qrY, 24, 24, C_TEAL);
    tft.fillRect(qrX + 3, qrY + 3, 6, 6, C_TEAL);
    tft.fillRect(qrX + 15, qrY + 3, 6, 6, C_TEAL);
    tft.fillRect(qrX + 3, qrY + 15, 6, 6, C_TEAL);
    tft.fillRect(qrX + 13, qrY + 13, 5, 5, C_TEAL);
    return;
  }

  // Tasks layout - 3 tasks + "Add" row
  const int contentTop = 60;
  const int rowH = 40;
  const int yBase = contentTop + 20;

  const int checkboxX = 28;
  const int textStartX = 52;

  for (int i = 0; i < 3; i++) {
    int baseline = yBase + i * rowH;
    bool isSelected = (i == tasksSel);
    if (!full && !isSelected && i != lastListSel)
      continue;
    if (!full)  // rows are 40 px apart; 12pt caps reach baseline - 17
      tft.fillRect(14, baseline - 18, 214, 32, C_BG);

    // Checkbox
    uint16_t boxC =
        model->taskDone[i] ? C_GREEN : (isSelected ? C_TEAL : C_MUTED);
    tft.drawRoundRect(checkboxX, baseline - 10, 18, 18, 3, boxC);
    if (model->taskDone[i]) {
      // Checkmark
      tft.drawLine(checkboxX + 4, baseline - 2, checkboxX + 7, baseline + 2,
                   C_GREEN);
      tft.drawLine(checkboxX + 5, baseline - 2, checkboxX + 8, baseline + 2,
                   C_GREEN);
      tft.drawLine(checkboxX + 7, baseline + 2, checkboxX + 14, baseline - 6,
                   C_GREEN);
      tft.drawLine(checkboxX + 8, baseline + 2, checkboxX + 15, baseline - 6,
                   C_GREEN);
    }

    // Focus minutes on the right (teal for the task the timer works on)
    bool hasMin = model->taskFocusMin[i] > 0 && model->tasks[i] != "Empty";
    if (hasMin) {
      rightTextFont(String(model->taskFocusMin[i]) + "m", 212, baseline,
                    (i == model->currentTask) ? C_TEAL : C_MUTED,
                    &FreeSans9pt7b);
    }

    // Task text - aligned LEFT, color based on selection
    String t = model->tasks[i];
    if (t == "Empty")
      t = "---";
    t = fitText(t, hasMin ? 120 : 160, &FreeSans12pt7b);

    // Color: teal if selected, muted if done, white otherwise
    uint16_t textColor;
    if (isSelected) {
      textColor = C_TEAL;
    } else if (model->taskDone[i]) {
      textColor = C_MUTED;
    } else {
      textColor = C_TEXT;
    }

    tft.setTextColor(textColor);
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(textStartX, baseline);
    tft.print(t);

    // Selection indicator - small arrow on the left
    if (isSelected) {
      tft.fillTriangle(18, baseline - 4, 18, baseline + 4, 24, baseline,
                       C_TEAL);
    }

    tft.setFont();
  }

  lastListSel = tasksSel;
  // "Add" row at the bottom
  int addBaseline = yBase + 3 * rowH;
  bool addSelected = (tasksSel == 3);
  if (!full)
    tft.fillRect(14, addBaseline - 18, 214, 32, C_BG);
  uint16_t addColor = addSelected ? C_TEAL : C_MUTED;

  // Plus icon
  tft.drawFastHLine(checkboxX + 4, addBaseline - 1, 10, addColor);
  tft.drawFastVLine(checkboxX + 9, addBaseline - 6, 10, addColor);

  tft.setTextColor(addColor);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(textStartX, addBaseline);
  tft.print("Add task...");

  if (addSelected) {
    tft.fillTriangle(18, addBaseline - 4, 18, addBaseline + 4, 24, addBaseline,
                     C_TEAL);
  }
  tft.setFont();
}

// Helper to get days in month
static int getDaysInMonth(int month, int year) {
  const int daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days = daysPerMonth[month];
  // Leap year check for February
  if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
    days = 29;
  }
  return days;
}

// Helper to get day of week for first of month (0=Sunday)
static int getFirstDayOfWeek(int month, int year) {
  // Zeller's congruence (adjusted for Sunday=0)
  if (month < 2) {
    month += 12;
    year--;
  }
  int k = year % 100;
  int j = year / 100;
  int h = (1 + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;
  return (h + 6) % 7; // Convert to Sunday=0
}

void TaskyUI::drawCalendar() {
  // Already on screen: repaint only the old and new selected day + footer
  bool full = !baseDrawn || calendarLoading;
  baseDrawn = true;
  if (full)
    clearScreen();

  // Show loading state
  if (calendarLoading) {
    centerTextFont("CALENDAR", 34, C_TEXT, &FreeSansBold12pt7b);
    tft.drawFastHLine(40, 46, 160, C_LINE);
    centerTextFont("Loading...", 130, C_TEAL, &FreeSans12pt7b);
    return;
  }

  // Get current date (non-blocking)
  time_t nowTime = time(nullptr);
  struct tm now;
  bool hasTime = (nowTime > 1577836800); // Valid if after 2020

  int currentDay, currentMonth, currentYear;
  if (hasTime) {
    localtime_r(&nowTime, &now);
    currentDay = now.tm_mday;
    currentMonth = now.tm_mon; // 0-11
    currentYear = now.tm_year + 1900;
  } else {
    currentDay = 1;
    currentMonth = 0;   // January
    currentYear = 2025; // Default 2025
  }

  // Initialize selection to today if not set
  if (calendarSelDay == 0) {
    calendarSelDay = currentDay;
  }

  // Month/Year header
  const char *monthNames[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  char headerBuf[16];
  snprintf(headerBuf, sizeof(headerBuf), "%s %d", monthNames[currentMonth],
           currentYear);
  if (full)
    centerTextFont(headerBuf, 28, C_TEXT, &FreeSansBold12pt7b);

  // Day headers
  const char *dayHeaders[] = {"S", "M", "T", "W", "T", "F", "S"};
  int cellW = 32;
  int cellH = 24;
  int gridX = (240 - 7 * cellW) / 2;
  int gridY = 55; // Lowered for better aesthetics

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_MUTED);
  for (int d = 0; d < 7 && full; d++) {
    int16_t tx1, ty1;
    uint16_t tw, th;
    tft.getTextBounds(dayHeaders[d], 0, 0, &tx1, &ty1, &tw, &th);
    int x = gridX + d * cellW + (cellW - tw) / 2;
    tft.setCursor(x, gridY);
    tft.print(dayHeaders[d]);
  }

  // Calculate grid
  int firstDayOfWeek = getFirstDayOfWeek(currentMonth, currentYear);
  int daysInMonth = getDaysInMonth(currentMonth, currentYear);

  // Clamp selection
  if (calendarSelDay > daysInMonth)
    calendarSelDay = daysInMonth;

  // Draw day numbers
  int day = 1;
  int startY = gridY + 16;

  for (int row = 0; row < 6 && day <= daysInMonth; row++) {
    for (int col = 0; col < 7 && day <= daysInMonth; col++) {
      if (row == 0 && col < firstDayOfWeek) {
        continue;
      }

      int x = gridX + col * cellW;
      int y = startY + row * cellH;

      bool hasEvent = (model->calendarEvents[day - 1] > 0);
      bool isToday = (day == currentDay);
      bool isSelected = (day == calendarSelDay);
      if (!full && !isSelected && day != lastListSel) {
        day++;
        continue;
      }
      if (!full)
        tft.fillRect(x + 1, y - 13, cellW - 2, cellH, C_BG);

      // Selection highlight (filled rectangle)
      if (isSelected) {
        tft.fillRoundRect(x + 2, y - 12, cellW - 4, cellH - 2, 3, C_TEAL);
      }
      // Today outline (if not selected)
      else if (isToday) {
        tft.drawRoundRect(x + 2, y - 12, cellW - 4, cellH - 2, 3, C_MUTED);
      }

      // Day number
      char dayStr[3];
      snprintf(dayStr, sizeof(dayStr), "%d", day);

      tft.setFont(&FreeSans9pt7b);
      int16_t tx1, ty1;
      uint16_t tw, th;
      tft.getTextBounds(dayStr, 0, 0, &tx1, &ty1, &tw, &th);

      // Color based on state and busyness
      uint16_t textColor;
      uint8_t eventsOnThisDay = model->calendarEvents[day - 1];
      if (isSelected) {
        textColor = C_BG; // Dark text on teal background
      } else if (hasEvent) {
        textColor = calendarDayColor(eventsOnThisDay);  // Color by busyness
      } else {
        textColor = C_TEXT;
      }

      tft.setTextColor(textColor);
      int textX = x + (cellW - tw) / 2;
      tft.setCursor(textX, y);
      tft.print(dayStr);

      // Event indicator dot (if not selected) - color by busyness
      if (hasEvent && !isSelected) {
        uint16_t dotColor = calendarDayColor(eventsOnThisDay);
        tft.fillCircle(x + cellW / 2, y + 5, 2, dotColor);
      }

      day++;
    }
    yield();
  }

  lastListSel = calendarSelDay;
  // Footer - show event count for selected day
  tft.setFont();
  if (!full)
    tft.fillRect(14, 202, 212, 22, C_BG);
  int eventsOnDay = model->calendarEvents[calendarSelDay - 1];
  if (eventsOnDay > 0) {
    char footerBuf[24];
    snprintf(footerBuf, sizeof(footerBuf), "%d event%s on day %d", eventsOnDay,
             eventsOnDay > 1 ? "s" : "", calendarSelDay);
    centerTextFont(footerBuf, 218, C_TEAL, &FreeSans9pt7b);
  } else if (model->cfg.calendarUrl.length() == 0) {
    centerTextFont("No calendar linked", 218, C_MUTED, &FreeSans9pt7b);
  } else {
    char footerBuf[24];
    snprintf(footerBuf, sizeof(footerBuf), "Day %d - No events",
             calendarSelDay);
    centerTextFont(footerBuf, 218, C_MUTED, &FreeSans9pt7b);
  }
}

void TaskyUI::drawDayTasks(int day) {
  clearScreen();

  // Check if viewing today
  time_t nowTime = time(nullptr);
  struct tm now;
  bool hasTime = (nowTime > 1577836800);
  int currentDay = hasTime ? (localtime_r(&nowTime, &now), now.tm_mday) : 1;
  bool isToday = (day == currentDay);

  // Header with day number
  char headerBuf[24];
  if (isToday) {
    snprintf(headerBuf, sizeof(headerBuf), "TODAY - DAY %d", day);
  } else {
    snprintf(headerBuf, sizeof(headerBuf), "DAY %d EVENTS", day);
  }
  centerTextFont(headerBuf, 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(30, 46, 180, C_LINE);

  int marginX = 24;
  int yPos = 68;
  int eventsOnDay = model->calendarEvents[day - 1];

  // Show detailed events from monthEvents (works for any day)
  int matchCount = 0;
  for (int i = 0; i < model->monthEventCount && yPos < 190; i++) {
    if (model->monthEvents[i].day != day) continue;
    CalendarEventItem& evt = model->monthEvents[i];
    matchCount++;

    // Time
    tft.setTextColor(C_TEAL);
    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(marginX, yPos);
    if (evt.isAllDay) {
      tft.print("All day");
    } else {
      tft.print(evt.startTime);
      if (strcmp(evt.endTime, "--:--") != 0) {
        tft.print(" - ");
        tft.print(evt.endTime);
      }
    }

    // Title (bold, full display)
    yPos += 22;
    tft.setTextColor(C_TEXT);
    tft.setFont(&FreeSansBold12pt7b);
    String title = String(evt.title);
    if (title.length() > 16) {
      // First line
      String line1 = title.substring(0, 16);
      tft.setCursor(marginX, yPos);
      tft.print(line1);
      // Second line if needed
      yPos += 20;
      String line2 = title.substring(16);
      if (line2.length() > 16) line2 = line2.substring(0, 16) + "..";
      tft.setCursor(marginX, yPos);
      tft.print(line2);
    } else {
      tft.setCursor(marginX, yPos);
      tft.print(title);
    }

    // Description (if available)
    if (strlen(evt.description) > 0) {
      yPos += 20;
      tft.setTextColor(C_MUTED);
      tft.setFont(&FreeSans9pt7b);
      String desc = String(evt.description);
      if (desc.length() > 24) desc = desc.substring(0, 24) + "..";
      tft.setCursor(marginX, yPos);
      tft.print(desc);
    }

    // Separator between events - extra spacing
    yPos += 24;
    tft.drawFastHLine(marginX, yPos - 8, W - 2 * marginX, C_LINE);
    yPos += 8;
  }

  if (matchCount == 0 && eventsOnDay > 0) {
    // Events exist but monthEvents full - show count
    char msgBuf[32];
    snprintf(msgBuf, sizeof(msgBuf), "%d event%s", eventsOnDay,
             eventsOnDay > 1 ? "s" : "");
    centerTextFont(msgBuf, 100, C_TEAL, &FreeSans12pt7b);
  } else if (matchCount == 0) {
    // No events
    centerTextFont("No events", 100, C_MUTED, &FreeSans12pt7b);
    if (model->cfg.calendarUrl.length() == 0) {
      centerTextFont("Link calendar in", 130, C_MUTED, &FreeSans9pt7b);
      centerTextFont("web portal", 150, C_MUTED, &FreeSans9pt7b);
    }
  }

  // Footer hint
  centerTextFont("Press to close", 220, C_MUTED, &FreeSans9pt7b);
  tft.setFont();
}

void TaskyUI::drawTaskDetail(int index) {
  if (index < 0 || index >= 3)
    return;

  String taskName = model->tasks[index];
  String taskDesc = model->taskDescription[index];
  bool isDone = model->taskDone[index];

  if (taskName.length() == 0 || taskName == "Empty")
    return;

  // Full screen overlay - OP-1 style
  clearScreen();

  // Header with status
  centerTextFont("TASK DETAIL", 32, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 42, 160, C_LINE);

  // Task name - large, prominent
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(isDone ? C_GREEN : C_TEAL);

  // Word wrap task name
  int nameY = 70;
  String remaining = taskName;
  while (remaining.length() > 0 && nameY < 100) {
    String line = remaining;
    if (line.length() > 16) {
      int sp = line.lastIndexOf(' ', 16);
      if (sp > 0)
        line = line.substring(0, sp);
      else
        line = line.substring(0, 16);
    }

    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((240 - w) / 2, nameY);
    tft.print(line);

    remaining = remaining.substring(line.length());
    remaining.trim();
    nameY += 26;
  }

  // Status indicator
  int statusY = nameY + 10;
  if (isDone) {
    centerTextFont("COMPLETED", statusY, C_GREEN, &FreeSans9pt7b);
  } else if (model->taskFocusMin[index] > 0) {
    String focused = "Focused " + String(model->taskFocusMin[index]) + " min";
    centerTextFont(focused.c_str(), statusY, C_TEAL, &FreeSans9pt7b);
  } else {
    centerTextFont("IN PROGRESS", statusY, C_MUTED, &FreeSans9pt7b);
  }

  // Divider
  tft.drawFastHLine(30, statusY + 15, 180, C_LINE);

  // Description (if any)
  int descY = statusY + 35;
  if (taskDesc.length() > 0) {
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(C_TEXT);

    // Word wrap description
    remaining = taskDesc;
    while (remaining.length() > 0 && descY < 182) {
      String line = remaining;
      if (line.length() > 22) {
        int sp = line.lastIndexOf(' ', 22);
        if (sp > 0)
          line = line.substring(0, sp);
        else
          line = line.substring(0, 22);
      }
      tft.setCursor(30, descY);
      tft.print(line);
      remaining = remaining.substring(line.length());
      remaining.trim();
      descY += 18;
    }
  } else {
    centerTextFont("No description", descY, C_MUTED, &FreeSans9pt7b);
    centerTextFont("Add via web portal", descY + 20, C_MUTED, &FreeSans9pt7b);
  }

  // Footer hints
  tft.drawFastHLine(40, 192, 160, C_LINE);
  centerTextFont(isDone ? "Click: close" : "Click: focus on it", 210, C_TEAL,
                 &FreeSans9pt7b);
  tft.setFont();
}

// Streak: two pages, yours (Tasky activity) and GitHub's, same days. Every
// cell is painted on each draw, so switching pages needs no clear (no flash).
void TaskyUI::drawStreak() {
  bool full = !baseDrawn;
  baseDrawn = true;
  bool gh = streakPage == 1;
  const uint8_t *grid = gh ? model->ghStreak : model->streak;
  if (full)
    clearScreen();

  // Header (below the carousel dots)
  tft.fillRect(0, 14, W, 34, C_BG);
  centerTextFont(gh ? "GITHUB" : "YOUR STREAK", 40, C_TEXT, &FreeSansBold12pt7b);

  const int sz = 12, gap = 2;
  const int gx = (W - (STREAK_COLS * sz + (STREAK_COLS - 1) * gap)) / 2, gy = 58;
  for (int r = 0; r < STREAK_ROWS; r++) {
    for (int c = 0; c < STREAK_COLS; c++) {
      int idx = r * STREAK_COLS + c;
      int x = gx + c * (sz + gap), y = gy + r * (sz + gap);
      tft.fillRect(x - 1, y - 1, sz + 2, sz + 2, C_BG);  // drops the old "today" ring
      tft.fillRoundRect(x, y, sz, sz, 2, streakColor(grid[idx]));
      if (idx == model->currentDayIndex)
        tft.drawRoundRect(x - 1, y - 1, sz + 2, sz + 2, 3, C_TEXT);
    }
    yield();
  }

  tft.fillRect(10, 144, 220, 86, C_BG);
  for (int i = 0; i < 2; i++) {  // which page: yours / GitHub
    if (i == streakPage)
      tft.fillCircle(CX - 7 + i * 14, 150, 3, C_GREEN);
    else
      tft.drawCircle(CX - 7 + i * 14, 150, 3, C_LINE);
  }
  char buf[40];
  if (gh && model->cfg.githubUser.length() == 0) {
    centerTextFont("Add your GitHub user", 178, C_TEXT, &FreeSans9pt7b);
    centerTextFont("tasky-flash.vercel.app", 202, C_TEAL, &FreeSans9pt7b);
    return;
  }
  snprintf(buf, sizeof(buf), "%d day streak", model->calculateCurrentStreak(gh));
  centerTextFont(buf, 176, C_GREEN, &FreeSans12pt7b);
  if (gh)
    snprintf(buf, sizeof(buf), "%d active days", model->calculateTotalActiveDays(true));
  else
    snprintf(buf, sizeof(buf), "Best %d  -  %d active days", model->cfg.bestStreak,
             model->calculateTotalActiveDays(false));
  centerTextFont(fitText(buf, 200, &FreeSans9pt7b).c_str(), 198, C_MUTED, &FreeSans9pt7b);
  String who = gh ? "@" + model->cfg.githubUser : String("Focus, tasks and agents");
  centerTextFont(fitText(who, 200, &FreeSans9pt7b).c_str(), 220, C_LINE, &FreeSans9pt7b);
}

uint16_t TaskyUI::streakColor(uint8_t v) {
  if (v == 0)
    return C_LINE;
  if (v == 1)
    return 0x0366; // GitHub greens: #006d32
  if (v == 2)
    return 0x2528; // #26a641
  return 0x3E8A;   // #39d353
}

uint16_t TaskyUI::calendarDayColor(uint8_t eventCount) {
  if (eventCount == 0)
    return C_TEXT;   // Normal text color
  if (eventCount == 1)
    return C_GREEN;  // Light day
  if (eventCount <= 3)
    return C_ORANGE; // Moderate
  return C_RED;      // Busy day (4+)
}

// ==================== TEXT HELPERS ====================
static const char *const DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed",
                                        "Thu", "Fri", "Sat"};
static const char *const MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr",
                                          "May", "Jun", "Jul", "Aug",
                                          "Sep", "Oct", "Nov", "Dec"};

static String formatDate(const struct tm &t) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%s, %s %d", DAY_NAMES[t.tm_wday],
           MONTH_NAMES[t.tm_mon], t.tm_mday);
  return String(buf);
}

static String daysText(int n) { return String(n) + (n == 1 ? " day" : " days"); }

// Truncate with ".." so the text fits in maxW pixels
String TaskyUI::fitText(const String &s, int maxW, const GFXfont *font) {
  tft.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  String t = s;
  while ((int)w > maxW && t.length() > 1) {
    t.remove(t.length() - 1);
    tft.getTextBounds(t + "..", 0, 0, &x1, &y1, &w, &h);
    if ((int)w <= maxW)
      t += "..";
  }
  tft.setFont();
  return t;
}

// "Label ............ value" row inside the safe area
void TaskyUI::drawRow(const char *label, const String &value, int baseline,
                      uint16_t valueColor) {
  tft.setFont(&FreeSans9pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  tft.setTextColor(C_MUTED);
  tft.setCursor(28, baseline);
  tft.print(label);
  tft.setFont();
  int maxW = 212 - (28 + (int)w + 12);
  rightTextFont(fitText(value, maxW, &FreeSans9pt7b), 212, baseline,
                valueColor, &FreeSans9pt7b);
}

// ==================== FOCUS MODE ====================
void TaskyUI::drawFocus() {
  // Already on screen: repaint the status line and the rows that changed
  bool full = !baseDrawn;
  baseDrawn = true;
  Config &cfg = model->cfg;
  if (full) {
    clearScreen();
    centerTextFont("FOCUS MODE", 34, C_TEAL, &FreeSansBold12pt7b);
    tft.drawFastHLine(60, 44, 120, C_LINE);
  } else {
    tft.fillRect(14, 54, 212, 22, C_BG);
  }

  if (model->isFocusModeActive())
    centerTextFont("Active now", 70, C_GREEN, &FreeSans9pt7b);
  else if (cfg.focusAutoMode)
    centerTextFont("Starts with the timer", 70, C_MUTED, &FreeSans9pt7b);
  else
    centerTextFont("Off", 70, C_RED, &FreeSans9pt7b);

  const char *labels[3] = {"Auto with timer", "Light signal", "Lock screens"};
  bool values[3] = {cfg.focusAutoMode, cfg.focusLight, cfg.focusLock};
  for (int i = 0; i < 3; i++) {
    int baseline = 104 + i * 30;
    bool sel = (i == focusSel);
    if (!full && !sel && i != lastListSel)
      continue;
    if (!full)
      tft.fillRect(20, baseline - 18, 200, 26, C_BG);
    if (sel)
      tft.drawRoundRect(20, baseline - 18, 200, 26, 6, C_TEAL);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(sel ? C_TEAL : C_TEXT);
    tft.setCursor(28, baseline);
    tft.print(labels[i]);
    tft.setFont();
    rightTextFont(values[i] ? "ON" : "OFF", 212, baseline,
                  values[i] ? C_GREEN : C_MUTED, &FreeSans9pt7b);
  }

  lastListSel = focusSel;
  if (!full)
    return;
  tft.drawFastHLine(20, 184, 200, C_LINE);
  char buf[40];
  snprintf(buf, sizeof(buf), "Today: %d sessions, %dm", cfg.dailyPomodoroCount,
           cfg.dailyFocusMin);
  centerTextFont(buf, 208, C_MUTED, &FreeSans9pt7b);
}

// ==================== QUICK SETTINGS ====================
// Long press on Home: the three things you change most, one press each.
void TaskyUI::drawQuick(int sel) {
  bool full = !baseDrawn;
  baseDrawn = true;
  Config &c = model->cfg;
  if (full) {
    clearScreen();
    centerTextFont("QUICK SETTINGS", 40, C_TEXT, &FreeSansBold12pt7b);
    tft.drawFastHLine(40, 50, 160, C_LINE);
    centerTextFont("Hold or MODE: back", 214, C_MUTED, &FreeSans9pt7b);
  }
  const char *labels[3] = {"Theme", "Light bar", "Focus light"};
  String values[3] = {c.darkMode ? "Dark" : "Light",
                      c.ledBright == 0 ? "Off" : c.ledBright < 150 ? "Low" : "High",
                      c.focusLight ? "On" : "Off"};
  for (int i = 0; i < 3; i++) {
    int baseline = 92 + i * 36;
    bool isSel = i == sel;
    tft.fillRect(18, baseline - 22, 204, 32, C_BG);
    if (isSel)
      tft.fillRoundRect(18, baseline - 22, 204, 32, 8, C_TEAL);
    tft.setFont(&FreeSans12pt7b);
    tft.setTextColor(isSel ? C_BG : C_TEXT);
    tft.setCursor(30, baseline);
    tft.print(labels[i]);
    tft.setFont();
    rightTextFont(values[i], 210, baseline, isSel ? C_BG : C_MUTED, &FreeSans12pt7b);
  }
}

// ==================== SCREENS EDITOR ====================
void TaskyUI::drawScreens(const char *start, const char *const labels[], const bool included[],
                          int count, int sel, bool grabbed) {
  const int visible = 6, rowH = 24, first = 80;
  bool full = !baseDrawn;
  baseDrawn = true;
  if (full) {
    clearScreen();
    centerTextFont("SCREENS", 34, C_TEXT, &FreeSansBold12pt7b);
    tft.drawFastHLine(40, 44, 160, C_LINE);
  }
  tft.fillRect(10, 50, 220, 182, C_BG);
  int rows = count + 1;  // + the start row
  int startRow = constrain(sel - visible / 2, 0, max(0, rows - visible));
  for (int v = 0; v < visible && startRow + v < rows; v++) {
    int r = startRow + v;
    int y = first + v * rowH;
    bool isSel = r == sel;
    uint16_t bg = isSel ? (grabbed ? C_ORANGE : C_TEAL) : C_BG;
    if (isSel)
      tft.fillRoundRect(16, y - 16, 208, 22, 5, bg);
    uint16_t fg = isSel ? C_BG : C_TEXT;
    tft.setFont(&FreeSans9pt7b);
    if (r == 0) {
      tft.setTextColor(isSel ? C_BG : C_TEAL);
      tft.setCursor(26, y);
      tft.print("Start");
      tft.setFont();
      rightTextFont(start, 214, y, isSel ? C_BG : C_TEAL, &FreeSans9pt7b);
      continue;
    }
    int i = r - 1;
    if (included[i])  // a tick, or an empty box
      tft.fillRoundRect(26, y - 11, 10, 10, 2, isSel ? C_BG : C_TEAL);
    else
      tft.drawRoundRect(26, y - 11, 10, 10, 2, isSel ? C_BG : C_MUTED);
    tft.setTextColor(included[i] ? fg : (isSel ? C_BG : C_MUTED));
    tft.setCursor(44, y);
    tft.print(labels[i]);
    tft.setFont();
    if (isSel && grabbed)
      rightTextFont("move", 214, y, C_BG, &FreeSans9pt7b);
  }
  centerTextFont(grabbed ? "Turn: move  Press: drop" : "Press: on/off  Hold: move", 226,
                 C_MUTED, &FreeSans9pt7b);
}

// ==================== MORE MENU ====================
void TaskyUI::drawMore(const char *const labels[], int count) {
  const int visible = 6, rowH = 22, first = 74;
  moreSel = constrain(moreSel, 0, count - 1);
  int start = constrain(moreSel - visible / 2, 0, max(0, count - visible));

  // Already on screen: repaint only the rows that changed (all rows if the
  // list scrolled)
  bool full = !baseDrawn;
  baseDrawn = true;
  bool scrolled = full || start != lastListStart;
  if (full) {
    clearScreen();
    centerTextFont("MORE", 34, C_TEXT, &FreeSansBold12pt7b);
    tft.drawFastHLine(40, 44, 160, C_LINE);
  }

  for (int v = 0; v < visible && start + v < count; v++) {
    int i = start + v;
    int baseline = first + v * rowH;
    bool sel = (i == moreSel);
    if (!scrolled && !sel && i != lastListSel)
      continue;
    if (!full)
      tft.fillRect(20, baseline - 16, 200, 22, C_BG);
    if (sel)
      tft.fillRoundRect(20, baseline - 15, 200, 20, 4, C_TEAL);
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(sel ? C_BG : C_TEXT);
    tft.setCursor(32, baseline);
    tft.print(labels[i]);
    tft.setCursor(202, baseline);
    tft.print(">");
    tft.setFont();
  }

  lastListSel = moreSel;
  lastListStart = start;

  // Scroll hints
  tft.fillTriangle(CX - 4, 54, CX + 4, 54, CX, 50, start > 0 ? C_MUTED : C_BG);
  tft.fillTriangle(CX - 4, 200, CX + 4, 200, CX, 205,
                   start + visible < count ? C_MUTED : C_BG);
}

// ==================== HOME / AMBIENT ====================
// Home (first screen) and the idle Ambient overlay share this glance layout:
// clock, date, weather, what's next, today's open tasks and a rotating phrase.
struct Phrase {
  const char *text, *author;  // author may be ""
};
static const Phrase PHRASES[] = {
    // Tasky
    {"One thing at a time", ""},
    {"Deep work, calm mind", ""},
    {"Small steps, every day", ""},
    {"Protect your focus", ""},
    {"Breathe. Then begin.", ""},
    {"Done is better than perfect", ""},
    // Philosophy
    {"What stands in the way becomes the way.", "Marcus Aurelius"},
    {"The impediment to action advances action.", "Marcus Aurelius"},
    {"Waste no more time arguing what a good man should be. Be one.", "Marcus Aurelius"},
    {"We suffer more often in imagination than in reality.", "Seneca"},
    {"Luck is what happens when preparation meets opportunity.", "Seneca"},
    {"He who has a why to live can bear almost any how.", "Friedrich Nietzsche"},
    {"Amor fati.", "Friedrich Nietzsche"},
    {"The unexamined life is not worth living.", "Socrates"},
    {"Well begun is half done.", "Aristotle"},
    {"We are what we repeatedly do.", "Will Durant"},
    {"It does not matter how slowly you go as long as you do not stop.", "Confucius"},
    {"The journey of a thousand miles begins with one step.", "Lao Tzu"},
    {"Fall seven times, stand up eight.", "Japanese proverb"},
    // Founders & startups
    {"Make something people want.", "Paul Graham"},
    {"Do things that don't scale.", "Paul Graham"},
    {"Ideas are easy. Execution is everything.", "John Doerr"},
    {"Real artists ship.", "Steve Jobs"},
    {"Stay hungry, stay foolish.", "Steve Jobs"},
    {"Focusing is about saying no.", "Steve Jobs"},
    {"Move fast and break things.", "Mark Zuckerberg"},
    {"The biggest risk is not taking any risk.", "Mark Zuckerberg"},
    {"Your most unhappy customers are your greatest source of learning.", "Bill Gates"},
    {"Chase the vision, not the money.", "Tony Hsieh"},
    {"If you are not embarrassed by the first version of your product, you've launched too late.", "Reid Hoffman"},
    {"The best way to predict the future is to invent it.", "Alan Kay"},
    {"Be so good they can't ignore you.", "Steve Martin"},
    // Builders
    {"Talk is cheap. Show me the code.", "Linus Torvalds"},
    {"Make it work, make it right, make it fast.", "Kent Beck"},
    {"Simplicity is prerequisite for reliability.", "Edsger Dijkstra"},
    {"Less is more.", "Mies van der Rohe"},
    {"Perfection is achieved when there is nothing left to take away.", "Antoine de Saint-Exupery"},
    // Motivation
    {"The secret of getting ahead is getting started.", "Mark Twain"},
    {"It always seems impossible until it's done.", "Nelson Mandela"},
    {"Start where you are. Use what you have. Do what you can.", "Arthur Ashe"},
    {"Discipline equals freedom.", "Jocko Willink"},
    {"You miss 100% of the shots you don't take.", "Wayne Gretzky"},
    {"Energy and persistence conquer all things.", "Benjamin Franklin"},
    {"Lost time is never found again.", "Benjamin Franklin"},
    {"Hard choices, easy life. Easy choices, hard life.", "Jerzy Gregorek"},
    {"The way to get started is to quit talking and begin doing.", "Walt Disney"},
    {"Whether you think you can, or you think you can't, you're right.", "Henry Ford"},
};
static const int PHRASES_N = sizeof(PHRASES) / sizeof(PHRASES[0]);

// Your own phrases (web setup, one per line) replace the built-in ones
int TaskyUI::phraseCount() {
  const String &p = model->cfg.phrases;
  if (p.length() == 0)
    return PHRASES_N;
  int n = 1;
  for (unsigned i = 0; i < p.length(); i++)
    if (p[i] == '\n')
      n++;
  return n;
}

// Phrase idx (wraps): text and author. Your own phrases may name an author
// after " - " ("Stay hungry - Steve Jobs").
void TaskyUI::phraseAt(int idx, String &text, String &author) {
  int n = phraseCount();
  idx = ((idx % n) + n) % n;
  const String &p = model->cfg.phrases;
  if (p.length() == 0) {
    text = PHRASES[idx].text;
    author = PHRASES[idx].author;
    return;
  }
  int start = 0;
  for (int k = 0; k < idx; k++)
    start = p.indexOf('\n', start) + 1;
  int end = p.indexOf('\n', start);
  text = p.substring(start, end < 0 ? p.length() : end);
  author = "";
  int dash = text.lastIndexOf(" - ");
  if (dash > 0) {
    author = text.substring(dash + 3);
    text = text.substring(0, dash);
  }
}

String TaskyUI::phraseAt(int idx) {
  String text, author;
  phraseAt(idx, text, author);
  return text;
}

int TaskyUI::wrapText(const String &s, int maxW, const GFXfont *font,
                      String lines[], int maxLines) {
  tft.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  int n = 0;
  String line;
  int pos = 0;
  while (pos <= (int)s.length()) {
    int sp = s.indexOf(' ', pos);
    if (sp < 0)
      sp = s.length();
    String word = s.substring(pos, sp);
    pos = sp + 1;
    if (word.length() == 0)
      continue;
    String trial = line.length() ? line + " " + word : word;
    tft.getTextBounds(trial, 0, 0, &x1, &y1, &w, &h);
    if ((int)w <= maxW || line.length() == 0) {
      line = trial;
      continue;
    }
    if (n == maxLines) {
      tft.setFont();
      return -1;
    }
    lines[n++] = line;
    line = word;
  }
  if (line.length()) {
    if (n == maxLines) {
      tft.setFont();
      return -1;
    }
    lines[n++] = line;
  }
  tft.setFont();
  return n;
}

// Every full-screen clear goes through here, so overlays that live on top of
// a screen (the carousel dots) know when to draw themselves again.
void TaskyUI::clearScreen() {
  tft.fillScreen(C_BG);
  fullClears++;
}

// A short notice across the top, over any screen (an agent needs you)
void TaskyUI::drawBanner(const char *text) {
  tft.fillRoundRect(14, 14, 212, 30, 15, 0xFE60);  // amber, like waiting agents
  centerTextFont(fitText(text, 190, &FreeSansBold12pt7b).c_str(), 36, 0x0000,
                 &FreeSansBold12pt7b);
}

// Where you are in the MODE carousel: small dots along the top edge
void TaskyUI::drawCarouselDots(int index, int count) {
  const int gap = 9, y = 8;
  int x0 = CX - (count - 1) * gap / 2;
  tft.fillRect(x0 - 4, y - 3, (count - 1) * gap + 8, 7, C_BG);
  for (int i = 0; i < count; i++)
    tft.fillCircle(x0 + i * gap, y, i == index ? 2 : 1, i == index ? C_TEAL : C_MUTED);
}

// Phrases screen: one phrase, centered, with its author underneath
void TaskyUI::drawPhrase(int idx) {
  bool full = !baseDrawn;
  baseDrawn = true;
  if (full)
    clearScreen();
  else
    tft.fillRect(0, 20, W, 214, C_BG);  // text, author and counter
  String phrase, author;
  phraseAt(idx, phrase, author);
  String lines[6];
  const GFXfont *font = &FreeSans12pt7b;
  int lineH = 26;
  int n = wrapText(phrase, 200, font, lines, 4);
  if (n < 0) {  // long quote: regular 9pt, up to 6 lines
    font = &FreeSans9pt7b;
    lineH = 20;
    n = wrapText(phrase, 200, font, lines, 6);
    if (n < 0) {
      n = 6;
      lines[5] = fitText(lines[5] + "...", 200, font);
    }
  }
  int blockH = n * lineH + (author.length() ? 26 : 0);
  int top = 108 - blockH / 2 + lineH - 6;  // a bit above center; baseline of line 1
  for (int i = 0; i < n; i++)
    centerTextFont(lines[i].c_str(), top + i * lineH, C_TEXT, font);
  if (author.length())
    centerTextFont(fitText("- " + author, 200, &FreeSans9pt7b).c_str(), top + (n - 1) * lineH + 28,
                   C_TEAL, &FreeSans9pt7b);
  char buf[12];
  int count = phraseCount();
  snprintf(buf, sizeof(buf), "%d/%d", ((idx % count) + count) % count + 1, count);
  rightTextFont(buf, 222, 222, C_MUTED, &FreeSans9pt7b);  // bottom-right corner
}

void TaskyUI::drawAmbientClock() {
  char buf[6] = "--:--";
  struct tm t;
  if (model->localNow(t))
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);

  tft.fillRect(20, 22, 200, 50, C_BG);
  tft.setFont(&FreeSansBold24pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  tft.setTextColor(C_TEXT);
  tft.setCursor((W - (int)w) / 2 - x1, 66);
  tft.print(buf);
  tft.setFont();
}

void TaskyUI::drawAmbientPhrase() {
  // New phrase every 2 minutes (or every boot-relative 2 min without NTP)
  struct tm t;
  uint32_t slot = model->localNow(t) ? (uint32_t)(t.tm_yday * 720 + (t.tm_hour * 60 + t.tm_min) / 2)
                                     : millis() / 120000UL;
  String phrase = "Press the knob to focus";  // first-minute hint
  if (millis() >= 60000UL || model->hasCurrentTask()) {
    // Home has one line: take the next phrase that fits it
    int n = phraseCount();
    for (int k = 0; k < n; k++) {
      phrase = phraseAt(slot + k);
      tft.setFont(&FreeSans9pt7b);
      int16_t x1, y1;
      uint16_t w, h;
      tft.getTextBounds(phrase, 0, 0, &x1, &y1, &w, &h);
      tft.setFont();
      if (w <= 200)
        break;
    }
  }
  tft.fillRect(0, 204, W, 26, C_BG);
  if (statusLine.length())
    centerTextFont(fitText(statusLine, 200, &FreeSans9pt7b).c_str(), 222,
                   statusColor ? statusColor : C_TEAL, &FreeSans9pt7b);
  else
    centerTextFont(fitText(phrase, 200, &FreeSans9pt7b).c_str(), 222, C_MUTED,
                   &FreeSans9pt7b);
}

// Today at a glance under the date: your streak square for today and a bar
// of focus minutes toward the daily goal (it turns green when you reach it)
void TaskyUI::drawDayBar() {
  const int y = 110, x0 = 52, x1 = 200;
  tft.fillRect(20, y - 6, 200, 12, C_BG);
  tft.fillRoundRect(32, y - 5, 10, 10, 2, streakColor(model->streak[model->currentDayIndex]));
  int goal = max(1, (int)model->cfg.dailyGoalMin);
  int w = (x1 - x0) * min((int)model->cfg.dailyFocusMin, goal) / goal;
  tft.fillRoundRect(x0, y - 2, x1 - x0, 4, 2, C_LINE);
  if (w > 0)
    tft.fillRoundRect(x0, y - 2, max(4, w), 4, 2,
                      model->cfg.dailyFocusMin >= goal ? 0x3E8A : C_TEAL);
}

// The rest screen (Ambient): big clock, date and a quote with its author.
// Calm on purpose; Home has the full glance.
void TaskyUI::drawRest() {
  clearScreen();
  drawAmbientClock();
  struct tm t;
  if (model->localNow(t))
    centerTextFont(formatDate(t).c_str(), 96, C_MUTED, &FreeSans9pt7b);
  restSlot = -1;
  drawRestQuote();
}

void TaskyUI::drawRestQuote() {
  struct tm t;
  int32_t slot = model->localNow(t) ? (int32_t)(t.tm_yday * 720 + (t.tm_hour * 60 + t.tm_min) / 2)
                                    : (int32_t)(millis() / 120000UL);
  if (slot == restSlot)
    return;  // a new quote every 2 minutes
  restSlot = slot;
  String text, author;
  phraseAt(slot, text, author);
  tft.fillRect(10, 104, 220, 126, C_BG);  // glyphs of a 4-line quote start at ~107
  String lines[4];
  int n = wrapText(text, 200, &FreeSans9pt7b, lines, 4);
  if (n < 0) {
    n = 4;
    lines[3] = fitText(lines[3] + "...", 200, &FreeSans9pt7b);
  }
  int y = 150 - (n - 1) * 10;
  for (int i = 0; i < n; i++)
    centerTextFont(lines[i].c_str(), y + i * 20, C_TEXT, &FreeSans9pt7b);
  if (author.length())
    centerTextFont(fitText("- " + author, 200, &FreeSans9pt7b).c_str(), y + n * 20 + 4,
                   C_TEAL, &FreeSans9pt7b);
}

void TaskyUI::drawAmbient(bool weatherOk, float temp, const char *desc,
                          bool celsius) {
  clearScreen();
  drawAmbientClock();

  struct tm t;
  bool hasTime = model->localNow(t);

  // "Wed 15 Oct  ·  21°C Cloudy" (the font has no degree glyph: draw a ring)
  String date = hasTime ? formatDate(t) : String("");
  if (weatherOk) {
    String num = String((int)lroundf(temp));
    String rest = String(celsius ? "C " : "F ") + desc;
    tft.setFont(&FreeSans9pt7b);
    int16_t x1, y1;
    uint16_t wd = 0, wn, wr, h;
    String datePart = date.length() ? date + "   " : String("");
    if (datePart.length())
      tft.getTextBounds(datePart, 0, 0, &x1, &y1, &wd, &h);
    tft.getTextBounds(num, 0, 0, &x1, &y1, &wn, &h);
    rest = fitText(rest, max(40, 200 - (int)wd - (int)wn - 7), &FreeSans9pt7b);
    tft.getTextBounds(rest, 0, 0, &x1, &y1, &wr, &h);
    int x = (W - (int)(wd + wn + 7 + wr)) / 2;
    tft.setTextColor(C_MUTED);
    tft.setCursor(x, 96);
    tft.print(datePart);
    x += wd;
    tft.setTextColor(C_TEAL);
    tft.setCursor(x, 96);
    tft.print(num);
    tft.drawCircle(x + wn + 3, 85, 2, C_TEAL);
    tft.setCursor(x + wn + 7, 96);
    tft.print(rest);
    tft.setFont();
  } else if (date.length()) {
    centerTextFont(date.c_str(), 96, C_MUTED, &FreeSans9pt7b);
  }
  drawDayBar();

  // Not set up yet: say where to do it, instead of an empty agenda
  if (model->cfg.wifiSSID.length() == 0) {
    centerTextFont("Set me up", 140, C_TEXT, &FreeSansBold12pt7b);
    centerTextFont("tasky-flash.vercel.app", 166, C_TEAL, &FreeSans9pt7b);
    centerTextFont("Phone: More > Setup (QR)", 190, C_MUTED, &FreeSans9pt7b);
    drawAmbientPhrase();
    return;
  }

  // What's next on the calendar
  int y = 134;
  if (hasTime) {
    const CalendarEventItem *ev = model->nextEventToday(t.tm_hour * 60 + t.tm_min);
    if (ev) {
      String next = String(ev->startTime) + "  " + ev->title;
      centerTextFont(fitText(next, 200, &FreeSans9pt7b).c_str(), y, C_TEXT,
                     &FreeSans9pt7b);
      y += 24;
    }
  }

  // Today's open tasks (up to what fits above the phrase)
  int shown = 0;
  tft.setFont(&FreeSans9pt7b);
  for (int i = 0; i < 3 && y <= 188; i++) {
    if (model->taskDone[i] || model->tasks[i] == "Empty" || model->tasks[i].length() == 0)
      continue;
    bool current = (i == model->currentTask);
    tft.fillCircle(30, y - 5, 3, current ? C_TEAL : C_MUTED);
    tft.setTextColor(current ? C_TEAL : C_TEXT);
    tft.setCursor(42, y);
    tft.print(fitText(model->tasks[i], 176, &FreeSans9pt7b));
    y += 22;
    shown++;
  }
  tft.setFont();
  if (y == 134)
    centerTextFont("Nothing scheduled", 150, C_MUTED, &FreeSans9pt7b);

  drawAmbientPhrase();
}

#if FEATURE_AGENTS
// ==================== AGENTS (agents edition) ====================
// Agents: one card per session with Tasky's little robot, or the permission
// question a Claude Code hook is waiting on. Turning between cards repaints the
// card area only; the robot and the countdown repaint just themselves.
static const int AG_SCALE = 5;
static const int AG_SPRITE_X = CX - SPRITE_W * AG_SCALE / 2, AG_SPRITE_Y = 40;

// Working = Claude's terracotta, waiting / asking = amber, done = green
uint16_t TaskyUI::agentColor(const AgentSession &s) {
  if (s.question.id[0] || s.state == AGENT_WAITING)
    return 0xFE60;  // amber
  return s.state == AGENT_DONE ? 0x3E8A : 0xDBAA;  // #39d353 / #D97757
}

void TaskyUI::drawAgentSprite(const AgentSession &s, int frame) {
  bool waiting = s.question.id[0] || s.state == AGENT_WAITING;
  bool done = !waiting && s.state == AGENT_DONE;
  const char *const *rows = waiting ? SPRITE_WAIT[frame & 1]
                                    : (done ? SPRITE_DONE[frame & 1] : SPRITE_WORK[frame & 1]);
  int lift = done && (frame & 1) ? 1 : 0;  // the "done" hop
  uint16_t col = agentColor(s);
  // One spare row above for the hop. Every pixel is painted, so a new frame
  // needs no clear (no flicker).
  for (int y = -1; y < SPRITE_H; y++) {
    int src = y + lift;
    const char *row = src >= 0 && src < SPRITE_H ? rows[src] : nullptr;
    for (int x = 0; x < SPRITE_W; x++)
      tft.fillRect(AG_SPRITE_X + x * AG_SCALE, AG_SPRITE_Y + y * AG_SCALE, AG_SCALE, AG_SCALE,
                   row && row[x] == '#' ? col : C_BG);
  }
}

void TaskyUI::drawQuestionOptions(const AgentQuestion &q) {
  for (int i = 0; i < q.optionCount; i++) {
    int y = 128 + i * 26;
    bool sel = i == q.sel;
    tft.fillRoundRect(30, y, 180, 22, 11, sel ? C_TEAL : C_BG);
    if (!sel)
      tft.drawRoundRect(30, y, 180, 22, 11, C_LINE);
    centerTextFont(q.options[i], y + 16, sel ? C_BG : C_TEXT, &FreeSans9pt7b);
  }
}

void TaskyUI::drawQuestionCountdown(const AgentQuestion &q) {
  int32_t left = (int32_t)(q.deadline - millis()) / 1000;
  char buf[32];
  snprintf(buf, sizeof(buf), "Hold: ask in terminal  %lds", (long)max((int32_t)0, left));
  tft.fillRect(10, 210, 220, 20, C_BG);
  centerTextFont(buf, 224, C_MUTED, &FreeSans9pt7b);
}

void TaskyUI::drawAgents(const AgentBoard &board, int sel) {
  bool full = !baseDrawn;
  baseDrawn = true;
  if (full)
    clearScreen();
  else
    tft.fillRect(0, 13, W, H - 13, C_BG);  // a new card (keeps the carousel dots)

  if (board.count == 0) {
    centerTextFont("AGENTS", 34, C_TEXT, &FreeSansBold12pt7b);
    tft.drawFastHLine(40, 44, 160, C_LINE);
    centerTextFont("No agents yet", 104, C_TEXT, &FreeSans9pt7b);
    centerTextFont("Connect Claude Code:", 136, C_MUTED, &FreeSans9pt7b);
    centerTextFont("tasky-flash.vercel.app", 160, C_TEAL, &FreeSans9pt7b);
    centerTextFont("Setup > Agents", 184, C_MUTED, &FreeSans9pt7b);
    return;
  }

  // Which card, as dots at the top
  for (int i = 0; i < board.count; i++) {
    int x = CX - (board.count - 1) * 7 + i * 14;
    if (i == sel)
      tft.fillCircle(x, 22, 4, agentColor(board.sessions[i]));
    else
      tft.drawCircle(x, 22, 3, C_LINE);
  }

  const AgentSession &s = board.sessions[sel];
  if (s.question.id[0]) {
    const AgentQuestion &q = s.question;
    centerTextFont(fitText(s.label, 200, &FreeSansBold12pt7b).c_str(), 48, C_TEXT,
                   &FreeSansBold12pt7b);
    String asks = String("wants to use ") + q.tool;
    centerTextFont(fitText(asks, 200, &FreeSans9pt7b).c_str(), 70, C_ORANGE, &FreeSans9pt7b);
    tft.drawRoundRect(14, 80, 212, 42, 8, C_LINE);
    String lines[2];
    int n = wrapText(q.detail, 196, &FreeSans9pt7b, lines, 2);
    if (n < 0) {  // longer than two lines: cut the second
      n = 2;
      lines[1] = fitText(lines[1] + "...", 196, &FreeSans9pt7b);
    }
    for (int i = 0; i < n; i++)
      centerTextFont(lines[i].c_str(), (n == 1 ? 106 : 97) + i * 18, C_TEXT, &FreeSans9pt7b);
    drawQuestionOptions(q);
    drawQuestionCountdown(q);
    return;
  }

  drawAgentSprite(s, 0);
  if (board.count > 1) {  // more cards: "turn the knob" arrows on both sides
    const int my = AG_SPRITE_Y + SPRITE_H * AG_SCALE / 2;
    tft.fillTriangle(30, my, 40, my - 9, 40, my + 9, C_MUTED);
    tft.fillTriangle(210, my, 200, my - 9, 200, my + 9, C_MUTED);
  }
  centerTextFont(fitText(s.label, 200, &FreeSansBold12pt7b).c_str(), 124, C_TEXT,
                 &FreeSansBold12pt7b);
  static const char *const STATE_TXT[] = {"Working", "Waiting for you", "Done"};
  centerTextFont(STATE_TXT[s.state], 148, agentColor(s), &FreeSans9pt7b);
  if (s.msg[0]) {
    String lines[2];
    int n = wrapText(s.msg, 200, &FreeSans9pt7b, lines, 2);
    if (n < 0) {
      n = 2;
      lines[1] = fitText(lines[1] + "...", 200, &FreeSans9pt7b);
    }
    for (int i = 0; i < n; i++)
      centerTextFont(lines[i].c_str(), 176 + i * 20, C_MUTED, &FreeSans9pt7b);
  }
}
#endif

#if FEATURE_BLE_HID
// ==================== CONTROLS (agents edition) ====================
// Controls: the key list as a wheel (previous / current / next), so it
// moves the way the knob turns. Turning repaints the wheel only.
void TaskyUI::drawControls(const char *prev, const char *key, const char *next,
                           const char *hint, bool grabbed, bool connected, bool windows) {
  bool full = !baseDrawn;
  baseDrawn = true;
  if (full) {
    clearScreen();
    centerTextFont(windows ? "CONTROLS  -  Windows" : "CONTROLS  -  Mac", 30, C_MUTED,
                   &FreeSans9pt7b);
    if (connected) {
      tft.fillCircle(26, 216, 4, C_GREEN);
      centerTextFont("  Bluetooth connected", 222, C_MUTED, &FreeSans9pt7b);
    } else {
      centerTextFont("Pair \"Tasky\" in Bluetooth", 222, C_ORANGE, &FreeSans9pt7b);
    }
  } else {
    tft.fillRect(0, 54, W, 148, C_BG);
  }
  centerTextFont(fitText(prev, 200, &FreeSans9pt7b).c_str(), 76, C_LINE, &FreeSans9pt7b);
  tft.drawFastHLine(40, 92, 160, C_LINE);
  centerTextFont(fitText(key, 220, &FreeSansBold12pt7b).c_str(), 124,
                 grabbed ? C_ORANGE : C_TEAL, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 140, 160, C_LINE);
  centerTextFont(fitText(next, 200, &FreeSans9pt7b).c_str(), 164, C_LINE, &FreeSans9pt7b);
  centerTextFont(hint, 194, grabbed ? C_ORANGE : C_MUTED, &FreeSans9pt7b);
}
#endif

// ==================== NOTICES ====================
void TaskyUI::drawMeetingAlert(const CalendarEventItem &ev, int minutes,
                               bool timerPaused) {
  clearScreen();
  centerTextFont("MEETING", 50, C_ORANGE, &FreeSansBold12pt7b);

  char buf[12];
  if (minutes <= 0)
    snprintf(buf, sizeof(buf), "Now");
  else
    snprintf(buf, sizeof(buf), "%d min", minutes);
  centerTextFont(buf, 112, C_TEXT, &FreeSansBold24pt7b);

  centerTextFont(fitText(ev.title, 200, &FreeSans9pt7b).c_str(), 150, C_TEXT,
                 &FreeSans9pt7b);
  String span = ev.startTime;
  if (TaskyModel::hhmmToMinutes(ev.endTime) >= 0)
    span += String(" - ") + ev.endTime;
  centerTextFont(span.c_str(), 174, C_MUTED, &FreeSans9pt7b);

  if (timerPaused)
    centerTextFont("Timer paused", 192, C_ORANGE, &FreeSans9pt7b);
  centerTextFont("Click to dismiss", 212, C_MUTED, &FreeSans9pt7b);
}

void TaskyUI::drawMorningBrief() {
  clearScreen();
  centerTextFont("GOOD MORNING", 40, C_TEAL, &FreeSansBold12pt7b);

  struct tm t;
  bool hasTime = model->localNow(t);
  if (hasTime)
    centerTextFont(formatDate(t).c_str(), 62, C_MUTED, &FreeSans9pt7b);

  int n = model->todayEventCount;
  drawRow("Events", n ? String(n) + " today" : String("Free day"), 96, C_TEXT);
  const CalendarEventItem *ev =
      model->nextEventToday(hasTime ? t.tm_hour * 60 + t.tm_min : 0);
  drawRow("Next", ev ? String(ev->startTime) + " " + ev->title : String("-"),
          122, C_TEXT);
  drawRow("Streak", daysText(model->calculateCurrentStreak()), 148, C_TEAL);
  drawRow("Yesterday", String(model->cfg.yesterdayPomodoro) + " sessions", 174,
          C_TEXT);
  centerTextFont("Click to start", 210, C_MUTED, &FreeSans9pt7b);
}

void TaskyUI::drawDayClose() {
  clearScreen();
  Config &cfg = model->cfg;
  centerTextFont("DAY DONE", 40, C_PURPLE, &FreeSansBold12pt7b);

  struct tm t;
  bool hasTime = model->localNow(t);
  if (hasTime)
    centerTextFont(formatDate(t).c_str(), 62, C_MUTED, &FreeSans9pt7b);

  int done = 0, total = 0;
  for (int i = 0; i < 3; i++) {
    if (model->tasks[i].length() == 0 || model->tasks[i] == "Empty")
      continue;
    total++;
    if (model->taskDone[i])
      done++;
  }

  drawRow("Focus", String(cfg.dailyFocusMin) + " min", 96, C_TEAL);
  drawRow("Sessions", String(cfg.dailyPomodoroCount), 122, C_TEXT);
  drawRow("Tasks", total ? String(done) + "/" + String(total) : String("-"),
          148, C_TEXT);
  drawRow("Streak", daysText(model->calculateCurrentStreak()), 174, C_TEAL);

  // Tomorrow's first meeting (month events only cover the current month)
  String footer = "Rest well";
  if (hasTime) {
    time_t tomorrow = time(nullptr) + 24 * 3600;
    struct tm tm2;
    localtime_r(&tomorrow, &tm2);
    const CalendarEventItem *ev =
        (tm2.tm_mon == t.tm_mon) ? model->firstEventOnDay(tm2.tm_mday) : nullptr;
    if (ev)
      footer = String("Tomorrow ") + ev->startTime + " " + ev->title;
  }
  centerTextFont(fitText(footer, 200, &FreeSans9pt7b).c_str(), 210, C_MUTED,
                 &FreeSans9pt7b);
}

// ==================== OTA UPDATE ====================
void TaskyUI::drawUpdate(bool armed, const String &url, const String &hint,
                         const String &version) {
  clearScreen();
  centerTextFont("UPDATE", 40, C_TEXT, &FreeSansBold12pt7b);
  centerTextFont(("v" + version).c_str(), 62, C_MUTED, &FreeSans9pt7b);

  if (!armed) {
    centerTextFont("Click to allow", 110, C_TEXT, &FreeSans9pt7b);
    centerTextFont("updates for 5 min", 132, C_MUTED, &FreeSans9pt7b);
    centerTextFont("Blocked otherwise", 200, C_MUTED, &FreeSans9pt7b);
    return;
  }

  centerTextFont("READY", 92, C_GREEN, &FreeSansBold12pt7b);
  centerTextFont("Open in browser", 124, C_MUTED, &FreeSans9pt7b);
  centerTextFont(fitText(url, 200, &FreeSans9pt7b).c_str(), 148, C_TEXT,
                 &FreeSans9pt7b);
  if (hint.length() > 0)
    centerTextFont(fitText(hint, 200, &FreeSans9pt7b).c_str(), 172, C_MUTED,
                   &FreeSans9pt7b);
}

void TaskyUI::drawUpdateCountdown(uint32_t remainingMs) {
  uint32_t s = remainingMs / 1000;
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu:%02lu left", (unsigned long)(s / 60),
           (unsigned long)(s % 60));
  tft.fillRect(40, 188, 160, 22, C_BG);
  centerTextFont(buf, 204, C_TEAL, &FreeSans9pt7b);
}

void TaskyUI::drawAgenda() {
  // Already on screen (new data, refresh): repaint the list only, keep the title
  bool full = !baseDrawn;
  baseDrawn = true;
  if (full) {
    clearScreen();
    centerTextFont("AGENDA", 32, C_TEXT, &FreeSansBold12pt7b);
    tft.drawFastHLine(40, 38, 160, C_LINE);
  } else {
    tft.fillRect(0, 40, W, H - 40, C_BG);
  }

  int marginX = 24;
  int yPos = 56;

  // Get current day
  time_t nowTime = time(nullptr);
  struct tm now;
  bool hasTime = (nowTime > 1577836800);
  int currentDay = 1;
  int currentMonth = 0;
  if (hasTime) {
    localtime_r(&nowTime, &now);
    currentDay = now.tm_mday;
    currentMonth = now.tm_mon;
  }
  int tomorrowDay = currentDay + 1;
  const int daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (tomorrowDay > daysPerMonth[currentMonth]) tomorrowDay = 1;

  Serial.printf("[Agenda] monthEventCount=%d, currentDay=%d, tomorrowDay=%d\n",
                model->monthEventCount, currentDay, tomorrowDay);

  // === TODAY section ===
  tft.setTextColor(C_TEAL);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setCursor(marginX, yPos);
  tft.print("Today");
  yPos += 6;

  // Count today's events from monthEvents
  int todayCount = 0;
  for (int i = 0; i < model->monthEventCount; i++) {
    if (model->monthEvents[i].day == currentDay) todayCount++;
  }
  Serial.printf("[Agenda] todayCount=%d\n", todayCount);

  if (todayCount > 0) {
    int shown = 0;
    for (int i = 0; i < model->monthEventCount && shown < 3 && yPos < 135; i++) {
      if (model->monthEvents[i].day != currentDay) continue;
      CalendarEventItem& evt = model->monthEvents[i];
      shown++;
      yPos += 20;

      // Time
      tft.setTextColor(C_MUTED);
      tft.setFont(&FreeSans9pt7b);
      tft.setCursor(marginX, yPos);
      if (evt.isAllDay) {
        tft.print("All day");
      } else {
        tft.print(evt.startTime);
      }

      // Title
      tft.setTextColor(C_TEXT);
      String title = String(evt.title);
      if (title.length() > 14) title = title.substring(0, 14) + "..";
      tft.setCursor(80, yPos);
      tft.print(title);
    }
    if (todayCount > 3) {
      yPos += 16;
      char moreBuf[16];
      snprintf(moreBuf, sizeof(moreBuf), "+%d more", todayCount - 3);
      tft.setTextColor(C_MUTED);
      tft.setFont(&FreeSans9pt7b);
      tft.setCursor(marginX, yPos);
      tft.print(moreBuf);
    }
  } else {
    yPos += 20;
    tft.setTextColor(C_LINE);
    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(marginX, yPos);
    tft.print("No events");
  }

  // === Divider ===
  yPos += 16;
  tft.drawFastHLine(marginX, yPos, W - 2 * marginX, C_LINE);
  yPos += 20;

  // === TOMORROW section ===
  tft.setTextColor(C_PURPLE);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setCursor(marginX, yPos);
  tft.print("Tomorrow");
  yPos += 6;

  // Count tomorrow's events
  int tomorrowCount = 0;
  for (int i = 0; i < model->monthEventCount; i++) {
    if (model->monthEvents[i].day == tomorrowDay) tomorrowCount++;
  }

  if (tomorrowCount > 0) {
    int shown = 0;
    for (int i = 0; i < model->monthEventCount && shown < 3 && yPos < 220; i++) {
      if (model->monthEvents[i].day != tomorrowDay) continue;
      CalendarEventItem& evt = model->monthEvents[i];
      shown++;
      yPos += 20;

      // Time
      tft.setTextColor(C_MUTED);
      tft.setFont(&FreeSans9pt7b);
      tft.setCursor(marginX, yPos);
      if (evt.isAllDay) {
        tft.print("All day");
      } else {
        tft.print(evt.startTime);
      }

      // Title
      tft.setTextColor(C_TEXT);
      String title = String(evt.title);
      if (title.length() > 14) title = title.substring(0, 14) + "..";
      tft.setCursor(80, yPos);
      tft.print(title);
    }
    if (tomorrowCount > 3) {
      yPos += 16;
      char moreBuf[16];
      snprintf(moreBuf, sizeof(moreBuf), "+%d more", tomorrowCount - 3);
      tft.setTextColor(C_MUTED);
      tft.setFont(&FreeSans9pt7b);
      tft.setCursor(marginX, yPos);
      tft.print(moreBuf);
    }
  } else {
    yPos += 20;
    tft.setTextColor(C_LINE);
    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(marginX, yPos);
    tft.print("No events");
  }

#if FEATURE_NOTION
  // Only show Notion section if configured and has tasks
  if (model->cfg.notionApiKey.length() > 0 && model->notionTaskCount > 0) {
    yPos += 16;
    tft.drawFastHLine(marginX, yPos, W - 2 * marginX, C_LINE);
    yPos += 20;

    tft.setTextColor(C_MUTED);
    tft.setFont(&FreeSansBold12pt7b);
    tft.setCursor(marginX, yPos);
    tft.print("Notion");
    yPos += 6;

    int maxNotionTasks = min(3, model->notionTaskCount);
    for (int i = 0; i < maxNotionTasks; i++) {
      NotionTaskItem& task = model->notionTasks[i];
      yPos += 20;

      uint16_t boxColor = task.done ? C_GREEN : C_MUTED;
      tft.drawRoundRect(marginX, yPos - 10, 14, 14, 2, boxColor);
      if (task.done) {
        tft.drawLine(marginX + 3, yPos - 4, marginX + 5, yPos - 2, C_GREEN);
        tft.drawLine(marginX + 5, yPos - 2, marginX + 11, yPos - 8, C_GREEN);
      }

      tft.setTextColor(task.done ? C_MUTED : C_TEXT);
      tft.setFont(&FreeSans9pt7b);
      String title = String(task.title);
      if (title.length() > 14) title = title.substring(0, 14) + "..";
      tft.setCursor(marginX + 20, yPos);
      tft.print(title);
    }
  }
#endif

  tft.setFont();
}

void TaskyUI::drawStats() {
  clearScreen();
  // No frame - cleaner look

  centerTextFont("STATS", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  // Focus Time - Main stat (centered vertically)
  uint32_t mins = model->cfg.totalFocusMin;
  int hours = mins / 60;
  int m = mins % 60;

  char buf[32];
  snprintf(buf, sizeof(buf), "%dh %02dm", hours, m);

  centerTextFont(buf, 110, C_TEXT, &FreeSansBold18pt7b);
  centerTextFont("Total Focus Time", 135, C_MUTED, &FreeSans9pt7b);

  // Today
  snprintf(buf, sizeof(buf), "%d sessions today", model->cfg.dailyPomodoroCount);
  centerTextFont(buf, 175, C_TEAL, &FreeSans12pt7b);
  snprintf(buf, sizeof(buf), "%d min focused today", model->cfg.dailyFocusMin);
  centerTextFont(buf, 202, C_MUTED, &FreeSans9pt7b);
}

#if FEATURE_SPOTIFY
void TaskyUI::drawSpotify(const char* track, const char* artist, uint32_t progressMs,
                          uint32_t durationMs, bool isPlaying, int volume, bool isActive) {
  clearScreen();

  // Header
  centerTextFont("NOW PLAYING", 28, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 38, 160, C_LINE);

  // Check if there's active playback
  if (!isActive || track == nullptr || strlen(track) == 0) {
#if TASKY_MODE_DEV
    centerTextFont("No active playback", 100, C_MUTED, &FreeSans12pt7b);
    centerTextFont("Start playing music", 130, C_MUTED, &FreeSans9pt7b);
    centerTextFont("on any device", 150, C_MUTED, &FreeSans9pt7b);
#else
    centerTextFont("Play music", 95, C_TEXT, &FreeSansBold12pt7b);
    centerTextFont("Spotify, YouTube, etc.", 118, C_MUTED, &FreeSans9pt7b);
    centerTextFont("Tasky will show the track", 143, C_MUTED, &FreeSans9pt7b);
    centerTextFont("(coming soon)", 160, C_MUTED, &FreeSans9pt7b);
#endif

    // Music icon placeholder
    int iconX = CX;
    int iconY = 190;
    tft.drawCircle(iconX, iconY, 20, C_LINE);
    // Music note symbol
    tft.fillCircle(iconX - 5, iconY + 5, 4, C_LINE);
    tft.drawLine(iconX - 5, iconY + 5, iconX - 5, iconY - 10, C_LINE);
    tft.drawLine(iconX - 5, iconY - 10, iconX + 5, iconY - 13, C_LINE);
    tft.fillCircle(iconX + 5, iconY + 2, 4, C_LINE);
    tft.drawLine(iconX + 5, iconY + 2, iconX + 5, iconY - 13, C_LINE);
    return;
  }

  // Track name - large and prominent
  String trackStr = String(track);
  if (trackStr.length() > 20) trackStr = trackStr.substring(0, 20) + "..";
  centerTextFont(trackStr.c_str(), 75, C_TEXT, &FreeSansBold12pt7b);

  // Artist name
  if (artist && strlen(artist) > 0) {
    String artistStr = String(artist);
    if (artistStr.length() > 24) artistStr = artistStr.substring(0, 24) + "..";
    centerTextFont(artistStr.c_str(), 100, C_TEAL, &FreeSans12pt7b);
  }

  // Progress bar
  int barX = 30;
  int barY = 125;
  int barW = 180;
  int barH = 6;

  // Background bar
  tft.fillRoundRect(barX, barY, barW, barH, 3, C_LINE);

  // Progress fill
  if (durationMs > 0) {
    int progressW = (int)((float)progressMs / (float)durationMs * barW);
    if (progressW > 0) {
      tft.fillRoundRect(barX, barY, progressW, barH, 3, C_GREEN);
    }
  }

  // Time indicators
  int progressSec = progressMs / 1000;
  int durationSec = durationMs / 1000;
  char timeBuf[16];

  // Current time
  snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", progressSec / 60, progressSec % 60);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_MUTED);
  tft.setCursor(barX, barY + 20);
  tft.print(timeBuf);

  // Total time
  snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", durationSec / 60, durationSec % 60);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(timeBuf, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(barX + barW - w, barY + 20);
  tft.print(timeBuf);

  // Play/Pause indicator with controls hint
  int controlY = 170;

  // Play/Pause icon in center
  if (isPlaying) {
    // Pause icon (two bars)
    tft.fillRect(CX - 12, controlY - 10, 8, 20, C_GREEN);
    tft.fillRect(CX + 4, controlY - 10, 8, 20, C_GREEN);
  } else {
    // Play icon (triangle)
    tft.fillTriangle(CX - 10, controlY - 12, CX - 10, controlY + 12, CX + 14, controlY, C_GREEN);
  }

  // Previous/Next hints
  tft.fillTriangle(CX - 50, controlY, CX - 35, controlY - 8, CX - 35, controlY + 8, C_MUTED);
  tft.fillRect(CX - 55, controlY - 8, 4, 16, C_MUTED);

  tft.fillTriangle(CX + 50, controlY, CX + 35, controlY - 8, CX + 35, controlY + 8, C_MUTED);
  tft.fillRect(CX + 51, controlY - 8, 4, 16, C_MUTED);

  // Volume bar at bottom
  int volBarY = 205;
  int volBarW = 100;
  int volBarH = 4;
  int volBarX = CX - volBarW / 2;

  // Volume icon
  tft.fillTriangle(volBarX - 20, volBarY + 2, volBarX - 12, volBarY - 4, volBarX - 12, volBarY + 8, C_MUTED);
  tft.fillRect(volBarX - 25, volBarY - 2, 6, 8, C_MUTED);

  // Volume bar background
  tft.fillRoundRect(volBarX, volBarY, volBarW, volBarH, 2, C_LINE);

  // Volume fill
  int volFillW = (volume * volBarW) / 100;
  if (volFillW > 0) {
    tft.fillRoundRect(volBarX, volBarY, volFillW, volBarH, 2, C_TEAL);
  }

  // Volume percentage
  snprintf(timeBuf, sizeof(timeBuf), "%d%%", volume);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_MUTED);
  tft.setCursor(volBarX + volBarW + 8, volBarY + 6);
  tft.print(timeBuf);

  // Footer - control hints
  centerTextFont("Vol: Turn | Click: Play", 218, C_LINE, &FreeSans9pt7b);
  tft.setFont();
}
#endif

#if FEATURE_WEATHER
// Weather without data says what's missing (never the clock: that's Home)
void TaskyUI::drawWeatherEmpty(const char *line1, const char *line2, bool busy) {
  clearScreen();
  centerTextFont("WEATHER", 32, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 38, 160, C_LINE);
  // a soft, filled cloud
  uint16_t c = busy ? C_TEAL : C_LINE;
  tft.fillCircle(104, 100, 16, c);
  tft.fillCircle(128, 92, 22, c);
  tft.fillCircle(150, 104, 13, c);
  tft.fillRoundRect(88, 100, 76, 17, 8, c);
  centerTextFont(line1, 156, C_TEXT, &FreeSans12pt7b);
  centerTextFont(fitText(line2, 210, &FreeSans9pt7b).c_str(), 184, C_MUTED, &FreeSans9pt7b);
}

void TaskyUI::drawWeather(float temp, float feelsLike, uint8_t humidity,
                          uint8_t icon, const char* desc, const char* city, bool celsius) {
  clearScreen();

  // Header
  centerTextFont("WEATHER", 32, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 38, 160, C_LINE);

  // Check if we have valid data (modo dev)
  if (city == nullptr || strlen(city) == 0) {
    centerTextFont("Not configured", 100, C_MUTED, &FreeSans12pt7b);
    centerTextFont("Add city in", 130, C_MUTED, &FreeSans9pt7b);
    centerTextFont("web portal", 150, C_MUTED, &FreeSans9pt7b);
    return;
  }

  // Weather icon (simple shapes based on icon type)
  // 0=clear_day, 1=clear_night, 2=cloudy, 3=rain, 4=thunder, 5=snow, 6=mist, 7=unknown
  int iconX = 60;
  int iconY = 75;

  switch (icon) {
    case 0: // Clear day - sun
      tft.fillCircle(iconX, iconY, 18, C_ORANGE);
      // Sun rays
      for (int i = 0; i < 8; i++) {
        float angle = i * 45.0 * 3.14159 / 180.0;
        int x1 = iconX + (int)(cos(angle) * 24);
        int y1 = iconY + (int)(sin(angle) * 24);
        int x2 = iconX + (int)(cos(angle) * 32);
        int y2 = iconY + (int)(sin(angle) * 32);
        tft.drawLine(x1, y1, x2, y2, C_ORANGE);
      }
      break;
    case 1: // Clear night - moon
      tft.fillCircle(iconX, iconY, 18, 0xFFE0); // Yellow
      tft.fillCircle(iconX + 8, iconY - 5, 14, C_BG); // Cut out for crescent
      break;
    case 2: // Cloudy
      tft.fillCircle(iconX - 10, iconY + 5, 14, C_MUTED);
      tft.fillCircle(iconX + 5, iconY, 16, C_MUTED);
      tft.fillCircle(iconX + 18, iconY + 5, 12, C_MUTED);
      break;
    case 3: // Rain
      tft.fillCircle(iconX - 8, iconY - 5, 12, C_MUTED);
      tft.fillCircle(iconX + 8, iconY - 5, 14, C_MUTED);
      // Rain drops
      for (int i = 0; i < 3; i++) {
        int dx = iconX - 12 + i * 12;
        tft.drawLine(dx, iconY + 10, dx - 3, iconY + 20, C_TEAL);
        tft.drawLine(dx + 1, iconY + 10, dx - 2, iconY + 20, C_TEAL);
      }
      break;
    case 4: // Thunder
      tft.fillCircle(iconX - 5, iconY - 5, 14, C_MUTED);
      tft.fillCircle(iconX + 10, iconY - 5, 12, C_MUTED);
      // Lightning bolt
      tft.fillTriangle(iconX, iconY + 5, iconX + 8, iconY + 5, iconX + 2, iconY + 18, 0xFFE0);
      tft.fillTriangle(iconX + 2, iconY + 12, iconX + 10, iconY + 12, iconX + 5, iconY + 25, 0xFFE0);
      break;
    case 5: // Snow
      tft.fillCircle(iconX - 8, iconY - 5, 12, C_MUTED);
      tft.fillCircle(iconX + 8, iconY - 5, 14, C_MUTED);
      // Snowflakes
      for (int i = 0; i < 3; i++) {
        int dx = iconX - 10 + i * 10;
        tft.fillCircle(dx, iconY + 15, 3, 0xFFFF);
      }
      break;
    case 6: // Mist
      for (int i = 0; i < 4; i++) {
        int ly = iconY - 15 + i * 10;
        tft.drawFastHLine(iconX - 20, ly, 40, C_MUTED);
      }
      break;
    default: // Unknown
      tft.drawCircle(iconX, iconY, 18, C_MUTED);
      tft.setCursor(iconX - 5, iconY + 5);
      tft.setTextColor(C_MUTED);
      tft.print("?");
      break;
  }

  // Temperature - large display on right side
  char tempBuf[16];
  snprintf(tempBuf, sizeof(tempBuf), "%.0f%c", temp, celsius ? 'C' : 'F');

  tft.setFont(&FreeSansBold24pt7b);
  tft.setTextColor(C_TEXT);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(tempBuf, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(240 - w - 20, 90);
  tft.print(tempBuf);
  tft.setFont();

  // Feels like
  snprintf(tempBuf, sizeof(tempBuf), "Feels %.0f%c", feelsLike, celsius ? 'C' : 'F');
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_MUTED);
  tft.setCursor(130, 110);
  tft.print(tempBuf);

  // Description - centered below icon/temp area
  if (desc && strlen(desc) > 0) {
    String descStr = String(desc);
    if (descStr.length() > 20) descStr = descStr.substring(0, 20) + "..";
    centerTextFont(descStr.c_str(), 145, C_TEAL, &FreeSans12pt7b);
  }

  // City name
  if (city && strlen(city) > 0) {
    String cityStr = String(city);
    if (cityStr.length() > 18) cityStr = cityStr.substring(0, 18) + "..";
    centerTextFont(cityStr.c_str(), 170, C_MUTED, &FreeSans9pt7b);
  }

  // Humidity
  snprintf(tempBuf, sizeof(tempBuf), "Humidity: %d%%", humidity);
  centerTextFont(tempBuf, 195, C_MUTED, &FreeSans9pt7b);

  // Last updated hint
  centerTextFont("Updates every 30 min", 220, C_LINE, &FreeSans9pt7b);
}
#endif

#if FEATURE_LLM
#include "llm_service.h"

void TaskyUI::drawAgent(int state, const char* response, int responseLen,
                        const char* errorMsg, const char* modeName, const char* modeDesc,
                        bool showingResponse, int scrollOffset) {
  clearScreen();

  // State: THINKING animation
  if (state == LLM_SENDING || state == LLM_THINKING) {
    // Header with mode name
    centerTextFont(modeName, 28, C_TEAL, &FreeSansBold12pt7b);
    tft.drawFastHLine(40, 38, 160, C_LINE);

    // "Thinking" label
    centerTextFont("Thinking", 110, C_TEAL, &FreeSansBold12pt7b);

    // 3 animated dots - blink sequentially
    int dotPhase = (millis() / 400) % 3;
    int dotY = 140;
    int dotSpacing = 24;
    int dotStartX = CX - dotSpacing;
    for (int d = 0; d < 3; d++) {
      int dx = dotStartX + d * dotSpacing;
      uint16_t dotColor = (d == dotPhase) ? C_TEAL : C_LINE;
      int dotSize = (d == dotPhase) ? 6 : 4;
      tft.fillCircle(dx, dotY, dotSize, dotColor);
    }

    centerTextFont("Press to cancel", 218, C_MUTED, &FreeSans9pt7b);
    return;
  }

  // State: ERROR (only shows for config issues)
  if (state == LLM_ERROR) {
    centerTextFont("AI MODE", 28, C_TEXT, &FreeSansBold12pt7b);
    tft.drawFastHLine(40, 38, 160, C_LINE);

    centerTextFont("Error", 90, C_RED, &FreeSansBold12pt7b);

    if (errorMsg && strlen(errorMsg) > 0) {
      tft.setFont(&FreeSans9pt7b);
      tft.setTextColor(C_MUTED);

      String remaining = String(errorMsg);
      int yPos = 120;
      while (remaining.length() > 0 && yPos < 190) {
        String line = remaining;
        if (line.length() > 24) {
          int sp = line.lastIndexOf(' ', 24);
          if (sp > 0) line = line.substring(0, sp);
          else line = line.substring(0, 24);
        }
        int16_t x1, y1;
        uint16_t w, h;
        tft.getTextBounds(line, 0, 0, &x1, &y1, &w, &h);
        tft.setCursor((240 - w) / 2, yPos);
        tft.print(line);
        remaining = remaining.substring(line.length());
        remaining.trim();
        yPos += 16;
      }
    }

    centerTextFont("Press to go back", 218, C_MUTED, &FreeSans9pt7b);
    tft.setFont();
    return;
  }

  // State: DONE - show response with styled layout
  if (state == LLM_DONE && showingResponse && responseLen > 0) {
    // Mode header with accent
    centerTextFont(modeName, 26, C_TEAL, &FreeSansBold12pt7b);

    // Decorative accent line (shorter, centered)
    int16_t hx1, hy1;
    uint16_t hw, hh;
    tft.setFont(&FreeSansBold12pt7b);
    tft.getTextBounds(modeName, 0, 0, &hx1, &hy1, &hw, &hh);
    int lineW = min((int)hw + 20, 180);
    tft.drawFastHLine((W - lineW) / 2, 36, lineW, C_TEAL);
    tft.setFont();

    // Response text - centered with margins
    String remaining = String(response);
    int yPos = 56 - scrollOffset;
    int lineHeight = 18;
    int maxY = 190;
    int marginX = 24;  // Left/right margin
    int maxChars = 20;  // Narrower for safe margins on 240px

    while (remaining.length() > 0) {
      String line = remaining;

      int nlPos = line.indexOf('\n');
      if (nlPos >= 0 && nlPos < maxChars) {
        line = line.substring(0, nlPos);
        remaining = remaining.substring(nlPos + 1);
      } else if ((int)line.length() > maxChars) {
        int sp = line.lastIndexOf(' ', maxChars);
        if (sp > 0) line = line.substring(0, sp);
        else line = line.substring(0, maxChars);
        remaining = remaining.substring(line.length());
        remaining.trim();
      } else {
        remaining = "";
      }

      if (yPos >= 44 && yPos <= maxY) {
        tft.setFont(&FreeSans9pt7b);
        tft.setTextColor(C_TEXT);

        // Detect bullet points (·, -, •) and add teal accent
        bool isBullet = (line.length() > 1 &&
                         (line[0] == '-' || line[0] == '*' ||
                          (uint8_t)line[0] == 0xC2));  // UTF-8 middle dot prefix
        if (isBullet) {
          tft.fillCircle(marginX, yPos - 4, 3, C_TEAL);
          tft.setCursor(marginX + 10, yPos);
        } else {
          // Center each line
          int16_t x1, y1;
          uint16_t tw, th;
          tft.getTextBounds(line, 0, 0, &x1, &y1, &tw, &th);
          int cx = (W - (int)tw) / 2;
          if (cx < marginX) cx = marginX;
          tft.setCursor(cx, yPos);
        }
        tft.print(line);
      }
      yPos += lineHeight;
    }

    // Footer with subtle separator
    tft.drawFastHLine(60, 200, 120, C_LINE);
    if (yPos > maxY + lineHeight) {
      centerTextFont("Turn to scroll", 218, C_MUTED, &FreeSans9pt7b);
    } else {
      centerTextFont("Press to go back", 218, C_MUTED, &FreeSans9pt7b);
    }

    tft.setFont();
    return;
  }

  // State: IDLE - show today's mode info (clean layout)
  centerTextFont("AI MODE", 28, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 38, 160, C_LINE);

  // AI model indicator
  const char* modelLabel = (model && model->cfg.llmProvider == 1) ? "GPT-4o Mini" : "Claude Haiku";
  centerTextFont(modelLabel, 58, C_LINE, &FreeSans9pt7b);

  // Mode label (small, muted)
  if (model && model->cfg.llmModeOverride < 0) {
    centerTextFont("Daily mode", 82, C_MUTED, &FreeSans9pt7b);
  } else {
    centerTextFont("Manual mode", 82, C_MUTED, &FreeSans9pt7b);
  }

  // Mode name (large, teal, centered)
  centerTextFont(modeName, 125, C_TEAL, &FreeSansBold18pt7b);

  // Footer
  if (!model || model->cfg.llmApiKey.length() == 0) {
    centerTextFont("Set API key in portal", 200, C_RED, &FreeSans9pt7b);
  } else {
    centerTextFont("Click: ask", 200, C_MUTED, &FreeSans9pt7b);
    centerTextFont("Hold: change mode", 218, C_MUTED, &FreeSans9pt7b);
  }
  tft.setFont();
}

void TaskyUI::drawAgentModeSelect(int selection, int8_t currentOverride) {
  clearScreen();

  centerTextFont("SELECT MODE", 28, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 38, 160, C_LINE);

  // 10 items: AUTO + 9 modes
  static const int TOTAL_MODES = 10;
  static const char* modeLabels[10] = {
    "AUTO", "FOCUS", "KICKSTART", "TIME",
    "CLARITY", "ORDER", "CLOSING", "MOVEMENT",
    "GYM", "RECIPES"
  };

  // Determine which item is currently active
  int activeIdx = (currentOverride < 0) ? 0 : (currentOverride + 1);

  // Scrollable list - show 5 items centered on selection
  int visibleCount = 5;
  int startIdx = selection - visibleCount / 2;
  if (startIdx < 0) startIdx = 0;
  if (startIdx > TOTAL_MODES - visibleCount) startIdx = TOTAL_MODES - visibleCount;

  int yStart = 52;
  int rowH = 28;

  for (int v = 0; v < visibleCount; v++) {
    int idx = startIdx + v;
    if (idx < 0 || idx >= TOTAL_MODES) continue;

    int baseline = yStart + v * rowH;
    bool isSel = (idx == selection);
    bool isActive = (idx == activeIdx);

    // Selection highlight
    if (isSel) {
      int boxW = 180;
      int boxX = (W - boxW) / 2;
      tft.fillRoundRect(boxX, baseline - 2, boxW, 24, 6, C_TEAL);
    }

    // Mode name - centered
    uint16_t labelColor = isSel ? C_BG : (isActive ? C_TEAL : C_TEXT);
    tft.setFont(&FreeSansBold12pt7b);
    tft.setTextColor(labelColor);
    int16_t x1, y1;
    uint16_t tw, th;
    tft.getTextBounds(modeLabels[idx], 0, 0, &x1, &y1, &tw, &th);
    int cx = (W - (int)tw) / 2;
    tft.setCursor(cx, baseline + 16);
    tft.print(modeLabels[idx]);

    // Active indicator (small dot on left)
    if (isActive && !isSel) {
      tft.fillCircle(22, baseline + 12, 3, C_TEAL);
    }
  }

  // Scroll indicators
  if (startIdx > 0) {
    tft.fillTriangle(CX - 5, 46, CX + 5, 46, CX, 41, C_MUTED);
  }
  if (startIdx + visibleCount < TOTAL_MODES) {
    tft.fillTriangle(CX - 5, 193, CX + 5, 193, CX, 198, C_MUTED);
  }

  // Footer
  centerTextFont("Click to select", 220, C_MUTED, &FreeSans9pt7b);
  tft.setFont();
}
#endif

void TaskyUI::drawWifi(bool wifiOn, String ssid, String pass, String ip) {
  clearScreen();
  // No frame - cleaner look

  centerTextFont("WIFI", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  // Connection status indicator
  if (wifiOn && ip != "192.168.4.1" && ip != "0.0.0.0") {
    // Connected to external network
    centerTextFont("Connected", 68, C_GREEN, &FreeSans12pt7b);

    // Show connected network info
    tft.setTextColor(C_MUTED);
    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(25, 95);
    tft.print("Network:");

    // Get connected SSID from model (stored config)
    tft.setTextColor(C_TEAL);
    tft.setFont(&FreeSans12pt7b);
    String connectedSSID = model->cfg.wifiSSID;
    if (connectedSSID.length() > 16)
      connectedSSID = connectedSSID.substring(0, 16) + "..";
    tft.setCursor(25, 117);
    tft.print(connectedSSID);

    // IP address
    centerTextFont(ip.c_str(), 150, C_TEAL, &FreeSans12pt7b);
  } else {
    // Not connected or AP mode only
    centerTextFont("Disconnected", 68, C_RED, &FreeSans12pt7b);
    centerTextFont("Scan QR to configure", 95, C_MUTED, &FreeSans9pt7b);
  }

  // AP Info section (always show for configuration)
  tft.drawFastHLine(30, 165, 180, C_LINE);
  centerTextFont("Configuration Portal", 182, C_MUTED, &FreeSans9pt7b);

  // SSID row
  tft.setTextColor(C_MUTED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(25, 202);
  tft.print("AP:");

  tft.setTextColor(C_SOFT);
  tft.setCursor(55, 202);
  tft.print(ssid);

  // Password
  tft.setTextColor(C_MUTED);
  tft.setCursor(25, 222);
  tft.print("Pass:");

  tft.setTextColor(C_SOFT);
  tft.setCursor(75, 222);
  tft.print(pass);

  tft.setFont();
}

void TaskyUI::drawQRFull(String ssid, String pass) {
  tft.fillScreen(0xFFFF);

#if FEATURE_QR_CODE
  String payload = "WIFI:S:" + ssid + ";T:WPA;P:" + pass + ";;";
  qrcodegen::QrCode qr =
      qrcodegen::QrCode::encodeText(payload.c_str(), ECC_LOW);

  int size = qr.getSize();
  int border = 1;
  int totalModules = size + (border * 2);

  // QR fills most of the screen; one text line below shows the credentials
  // (the password is unique per device, so people need to be able to read it)
  const int TEXT_H = 22;
  int scale = (220 - TEXT_H) / totalModules;
  if (scale < 1)
    scale = 1;

  int dimension = totalModules * scale;
  int startX = (W - dimension) / 2;
  int startY = (H - dimension - TEXT_H) / 2;

  for (int y = -border; y < size + border; y++) {
    for (int x = -border; x < size + border; x++) {
      bool isBlack = false;
      if (x >= 0 && x < size && y >= 0 && y < size) {
        if (qr.getModule(x, y)) {
          isBlack = true;
        }
      }
      uint16_t color =
          isBlack ? 0x0000 : 0xFFFF; // Black QR for better scanning
      tft.fillRect(startX + (x + border) * scale, startY + (y + border) * scale,
                   scale, scale, color);
    }
  }
  String creds = ssid + " / " + pass;
  centerTextFont(creds.c_str(), startY + dimension + 17, 0x0000, &FreeSans9pt7b);
#else
  // Text-only fallback when QR disabled to save ~35-50 KB
  centerTextFont("WiFi Setup", 50, 0x0000, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 65, 160, 0x3186);
  centerTextFont("Use web portal to scan QR", 100, 0x3186, &FreeSans9pt7b);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(0x0000);
  tft.setCursor(30, 140);
  tft.print("SSID: ");
  tft.print(ssid);
  tft.setCursor(30, 170);
  tft.print("Pass: ");
  tft.print(pass);
  tft.setFont();
#endif
}

void TaskyUI::drawSplashTasky() {
  // PSP-style intro: letters appear one by one with fade
  clearScreen();

  tft.setFont(&FreeSansBold24pt7b);
  const char *letters = "Tasky";

  // Calculate total width for centering
  int16_t x1, y1;
  uint16_t totalW, h;
  tft.getTextBounds(letters, 0, 0, &x1, &y1, &totalW, &h);

  // Get individual letter positions
  int letterX[5];
  int baseX = (W - totalW) / 2;
  int cumX = baseX;

  for (int i = 0; i < 5; i++) {
    letterX[i] = cumX;
    char buf[2] = {letters[i], 0};
    uint16_t lw;
    tft.getTextBounds(buf, 0, 0, &x1, &y1, &lw, &h);
    cumX += lw + 2; // Letter spacing
  }

  // Fade colors: dark to bright
  uint16_t fadeSteps[] = {0x0841, 0x1082, 0x18C3, 0x2104, 0x3186, 0x4208,
                          0x52AA, 0x6B4D, 0x8410, 0xAD55, 0xD69A, 0xFFFF};
  int numFades = 12;

  // Each letter appears with fade animation
  for (int letterIdx = 0; letterIdx < 5; letterIdx++) {
    char buf[2] = {letters[letterIdx], 0};

    // Fade in this letter
    for (int f = 0; f < numFades; f++) {
      tft.setTextColor(fadeSteps[f]);
      tft.setCursor(letterX[letterIdx], 130);
      tft.print(buf);
      delay(15);  // Fast fade per step
      yield();    // Prevent watchdog during splash
    }

    // Brief pause between letters
    delay(80);
    yield();
  }

  // Accent line under logo
  delay(200);
  for (int w = 0; w <= 60; w += 4) {
    tft.drawFastHLine(120 - w / 2, 145, w, C_TEAL);
    delay(10);
    yield();
  }

  // Tagline fades in
  delay(200);
  uint16_t tagFades[] = {0x2104, 0x4208, 0x630C, C_MUTED};
  for (int f = 0; f < 4; f++) {
    centerTextFont("Focus. Flow. Finish.", 175, tagFades[f], &FreeSans9pt7b);
    delay(50);
    yield();
  }

  delay(400);
}

void TaskyUI::centerTextFont(const char *s, int baselineY, uint16_t color,
                             const GFXfont *font) {
  tft.setTextColor(color);
  tft.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds((char *)s, 0, baselineY, &x1, &y1, &w, &h);
  int x = (W - (int)w) / 2;
  tft.setCursor(x, baselineY);
  tft.print(s);
  tft.setFont();
}

void TaskyUI::rightTextFont(const String &s, int xRight, int baselineY,
                            uint16_t color, const GFXfont *font) {
  tft.setTextColor(color);
  tft.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds((char *)s.c_str(), 0, baselineY, &x1, &y1, &w, &h);
  tft.setCursor(xRight - (int)w, baselineY);
  tft.print(s);
  tft.setFont();
}

// ==================== TOUR & HINTS ====================
// Tour: each step shows the control it teaches and waits for that action.
void TaskyUI::drawTour(int step) {
  clearScreen();
  for (int i = 0; i < 4; i++) {  // progress dots
    int x = CX - 24 + i * 16;
    if (i <= step)
      tft.fillCircle(x, 26, 4, C_TEAL);
    else
      tft.drawCircle(x, 26, 4, C_LINE);
  }
  if (step == 3) {
    centerTextFont("You're set!", 84, C_TEXT, &FreeSansBold18pt7b);
    centerTextFont("MODE: next screen", 124, C_MUTED, &FreeSans9pt7b);
    centerTextFont("MODE twice: go back", 148, C_MUTED, &FreeSans9pt7b);
    centerTextFont("Hold MODE: rest screen", 172, C_MUTED, &FreeSans9pt7b);
    centerTextFont("Press the knob for Home", 212, C_TEAL, &FreeSans9pt7b);
    return;
  }
  static const char *const TITLE[] = {"Turn the knob", "Press the knob", "Press MODE"};
  static const char *const SUB[] = {"to move and change", "to pick, start or stop",
                                    "the button on top"};
  const int iy = 96;  // icon center
  if (step < 2) {     // the knob seen from above
    tft.drawCircle(CX, iy, 30, C_TEAL);
    tft.drawCircle(CX, iy, 29, C_TEAL);
    tft.fillCircle(CX, iy - 20, 4, C_TEAL);  // grip mark
  } else {            // the MODE button
    tft.drawRoundRect(CX - 36, iy - 16, 72, 32, 8, C_TEAL);
    tft.drawRoundRect(CX - 35, iy - 15, 70, 30, 7, C_TEAL);
    centerTextFont("MODE", iy + 6, C_TEXT, &FreeSans9pt7b);
  }
  if (step == 0) {  // rotation arrows on both sides
    tft.fillTriangle(CX - 48, iy + 2, CX - 36, iy + 2, CX - 42, iy - 10, C_TEAL);
    tft.fillTriangle(CX + 36, iy - 2, CX + 48, iy - 2, CX + 42, iy + 10, C_TEAL);
  } else {          // press arrow from above
    int top = step == 1 ? iy - 58 : iy - 46;
    tft.fillRect(CX - 3, top, 6, 10, C_TEAL);
    tft.fillTriangle(CX - 9, top + 10, CX + 9, top + 10, CX, top + 20, C_TEAL);
  }
  centerTextFont(TITLE[step], 160, C_TEXT, &FreeSansBold12pt7b);
  centerTextFont(SUB[step], 184, C_MUTED, &FreeSans9pt7b);
  centerTextFont("Hold MODE to skip", 218, C_MUTED, &FreeSans9pt7b);
}

// Hint pill. "Turn:", "Press:" and "Hold:" become small knob glyphs, so
// "Turn: time  Press: start" reads as (o) time   (*) start.
static int hintGlyph(const String &word) {
  if (word == "Turn:") return 1;
  if (word == "Press:") return 2;
  if (word == "Hold:") return 3;
  return 0;
}

void TaskyUI::drawHint(const char *text) {
  tft.fillRoundRect(10, 200, 220, 26, 13, C_TEXT);
  // Split into "Word: action" parts separated by two spaces
  String parts[3];
  int n = 0;
  String s = text;
  while (n < 3 && s.length()) {
    int cut = s.indexOf("  ");
    parts[n++] = cut < 0 ? s : s.substring(0, cut);
    s = cut < 0 ? "" : s.substring(cut + 2);
    s.trim();
  }
  // Measure: a glyph is 12 px + 4 px gap
  tft.setFont(&FreeSans9pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  int widths[3], glyphs[3], total = 0;
  String rest[3];
  for (int i = 0; i < n; i++) {
    int sp = parts[i].indexOf(' ');
    glyphs[i] = sp > 0 ? hintGlyph(parts[i].substring(0, sp)) : 0;
    rest[i] = glyphs[i] ? parts[i].substring(sp + 1) : parts[i];
    tft.getTextBounds(rest[i], 0, 0, &x1, &y1, &w, &h);
    widths[i] = w + (glyphs[i] ? 16 : 0);
    total += widths[i] + (i ? 14 : 0);
  }
  int x = (W - total) / 2;
  for (int i = 0; i < n; i++) {
    if (glyphs[i]) {
      int cx = x + 6, cy = 213;
      if (glyphs[i] == 1) {         // turn: ring with an arrow tip
        tft.drawCircle(cx, cy, 5, C_BG);
        tft.fillTriangle(cx + 3, cy - 7, cx + 7, cy - 4, cx + 2, cy - 3, C_BG);
      } else if (glyphs[i] == 2) {  // press: ring with a dot
        tft.drawCircle(cx, cy, 5, C_BG);
        tft.fillCircle(cx, cy, 2, C_BG);
      } else {                      // hold: solid knob
        tft.fillCircle(cx, cy, 5, C_BG);
      }
      x += 16;
    }
    tft.setTextColor(C_BG);
    tft.setCursor(x, 218);
    tft.print(rest[i]);
    x += widths[i] - (glyphs[i] ? 16 : 0) + 14;
  }
  tft.setFont();
}
