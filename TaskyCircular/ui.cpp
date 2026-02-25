#include "ui.h"
#include "focus_dns.h"
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
static const int MARGIN = 16;
static const int PWM_FREQ = 5000;
static const int PWM_RES = 8;

static const uint16_t C_FLASH = 0x39E7;
static const uint16_t C_FLASH2 = 0x1082;
static const int PROG_SEG_N = 12;

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
  tft.fillScreen(C_BG);
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

// Draw Frame Abstraction - Always circular for TE style
void TaskyUI::drawFrame() {
  // Always draw circular frame for TE aesthetic (works on both displays)
  tft.drawCircle(CX, CY, 119, C_LINE);
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

  // NOTE: Visual flash is only enabled if explicitly desired.
  // We'll separate this logic. This function is called for feedback.
  // If we want visual flash, we set flashOn.
  // For encoder rotation, caller should call a different function or we check
  // args? User wanted NO LED on encoder. So we only enable flashOn here if it's
  // NOT just simple rotation. Actually, we'll make flashOn controlled by
  // argument in a wrapper or change usage. For now, let's say uiTick is sound
  // only + flash. We will overload or change call sites for encoder.

  flashOn = true;
  flashUntilMs = now + (strong ? 80 : 50);  // Mas corto = menos parpadeo
}

void TaskyUI::drawFlashIfNeeded() {
  if (!flashOn)
    return;
  if (millis() > flashUntilMs) {
    tft.drawCircle(CX, CY, 119, C_LINE);
    flashOn = false;
    return;
  }
  // Flash mas sutil - solo borde, duracion corta
  tft.drawCircle(CX, CY, 119, C_FLASH);
}

uint16_t TaskyUI::accentColor() {
  return (model->phase == PHASE_WORK) ? C_TEAL : C_ORANGE;
}

// Helper to get color based on progress for ring
uint16_t getRingColor(float pct) {
  // pct 0.0 (empty) to 1.0 (full)
  if (pct > 0.66)
    return 0x2E57; // Teal/Blue-ish
  if (pct > 0.25)
    return 0xFD20; // Orange/Yellow
  return 0xF800;   // Red
}

void TaskyUI::drawProgressSegments() {
  uint32_t dur = model->phaseDurationMs();
  uint32_t rem = model->remainingMs();

  if (dur == 0)
    dur = 1;

  float pct = (float)rem / (float)dur;
  int totalSegs = 120;
  int segsOn = (int)(pct * totalSegs);
  if (segsOn < 0)
    segsOn = 0;
  if (segsOn > totalSegs)
    segsOn = totalSegs;

  // Smooth arc: radial line segments from inner to outer radius
  int rInner = 112;
  int rOuter = 116;
  uint16_t activeColor = getRingColor(pct);

  for (int i = 0; i < totalSegs; i++) {
    // Start from top (12 o'clock), go clockwise
    float angle = -90.0 + (360.0 / totalSegs) * i;
    float rad = angle * 3.14159 / 180.0;
    float cosA = cos(rad);
    float sinA = sin(rad);

    int x1 = CX + (int)(cosA * rInner);
    int y1 = CY + (int)(sinA * rInner);
    int x2 = CX + (int)(cosA * rOuter);
    int y2 = CY + (int)(sinA * rOuter);

    uint16_t color = (i >= totalSegs - segsOn) ? activeColor : C_LINE;
    tft.drawLine(x1, y1, x2, y2, color);

    // Yield every 30 segments to keep system responsive
    if (i % 30 == 0)
      yield();
  }
}

void TaskyUI::drawTimerBase() {
  tft.fillScreen(C_BG);
  flashOn = false;

  drawFrame();

  // Reset states
  baseDrawn = true;
  lastShownMin = -1;
  lastShownSec = -1;
  lastProgSegOn = -1;

  // Force draw segments immediately
  drawProgressSegments();
}

void TaskyUI::drawFinishScreen(const char *msg) {
  tft.fillScreen(C_BG);
  drawFrame();

  // Big centered text "WELL DONE" or similar
  tft.setTextColor(getRingColor(0.1)); // Red/Orange for attention? Or Green?
  // Actually "Finished" is good -> Green or Teal?
  // Let's use Teal or Text color.

  centerTextFont(msg, 130, C_TEXT, &FreeSansBold12pt7b);
}

void TaskyUI::drawTimerTime(int mm, int ss) {
  // Region to clear for time text
  // 24pt font digits are about 35px high.
  // Center Y is 120. Text bounds need checking.

  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);

  tft.setTextColor(C_TEXT);
  tft.setFont(&FreeSansBold24pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);

  // Calculate center position
  int cx = (W - w) / 2 - x1;     // x1 is usually 0 or small offset
  int cy = (H - h) / 2 + h - 10; // 10 is visual adjustment for baseline

  // Clear previous text area
  tft.fillRect(CX - 85, CY - 35, 170, 70, C_BG);
  tft.setCursor(cx, cy);
  tft.print(buf);
  tft.setFont();

  // Status (Paused/Done)
  tft.fillRect(40, 160, 160, 30, C_BG);
  if (model->st.finished) {
    centerTextFont("DONE", 180, C_RED, &FreeSansBold12pt7b);
  } else if (!model->st.running) {
    centerTextFont("PAUSED", 180, C_MUTED, &FreeSansBold12pt7b);
  }

  // Progress: solo redibujar cuando cambia el segmento (evita lentitud)
  uint32_t dur = model->phaseDurationMs();
  int segOn = (dur > 0) ? (int)((float)model->remainingMs() / dur * 60) : 0;
  if (segOn != lastProgSegOn) {
    lastProgSegOn = segOn;
    drawProgressSegments();
  }
}

void TaskyUI::drawSettings() {
  tft.fillScreen(C_BG);
  // No circle frame in settings - clean look

  centerTextFont("SETTINGS", 32, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(30, 46, 180, C_LINE);

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
    rightTextFont(values[i], 212, baseline, valueColor, &FreeSans12pt7b);
  }
}

void TaskyUI::drawTasks() {
  tft.fillScreen(C_BG);
  // No frame for tasks - cleaner look

  centerTextFont("TASKS", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  // Check if all tasks are empty (no real content)
  bool allEmpty = true;
  for (int i = 0; i < 3; i++) {
    if (model->tasks[i].length() > 0 && model->tasks[i] != "Empty")
      allEmpty = false;
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

  // Tasks layout - vertically centered in available space (after header ~50px
  // to ~220px) Available height: 170px, 3 tasks with spacing
  const int contentTop = 60;
  const int contentBottom = 220;
  const int rowH = 45;
  const int totalHeight = 3 * rowH;
  const int yBase =
      contentTop + (contentBottom - contentTop - totalHeight) / 2 + 25;

  const int checkboxX = 28;
  const int textStartX = 52;

  for (int i = 0; i < 3; i++) {
    int baseline = yBase + i * rowH;
    bool isSelected = (i == tasksSel);

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

    // Task text - aligned LEFT, color based on selection
    String t = model->tasks[i];
    if (t == "Empty")
      t = "---";
    if (t.length() > 15)
      t = t.substring(0, 15) + "..";

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

    // Selection indicator - small arrow or dot on the left
    if (isSelected) {
      tft.fillTriangle(18, baseline - 4, 18, baseline + 4, 24, baseline,
                       C_TEAL);
    }

    tft.setFont();
  }
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
  tft.fillScreen(C_BG);
  // No frame - cleaner look

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
  centerTextFont(headerBuf, 28, C_TEXT, &FreeSansBold12pt7b);

  // Day headers
  const char *dayHeaders[] = {"S", "M", "T", "W", "T", "F", "S"};
  int cellW = 32;
  int cellH = 24;
  int gridX = (240 - 7 * cellW) / 2;
  int gridY = 55; // Lowered for better aesthetics

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_MUTED);
  for (int d = 0; d < 7; d++) {
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

  // Footer - show event count for selected day
  tft.setFont();
  int eventsOnDay = model->calendarEvents[calendarSelDay - 1];
  if (eventsOnDay > 0) {
    char footerBuf[24];
    snprintf(footerBuf, sizeof(footerBuf), "%d event%s on day %d", eventsOnDay,
             eventsOnDay > 1 ? "s" : "", calendarSelDay);
    centerTextFont(footerBuf, 225, C_TEAL, &FreeSans9pt7b);
  } else if (model->cfg.calendarUrl.length() == 0) {
    centerTextFont("No calendar linked", 225, C_MUTED, &FreeSans9pt7b);
  } else {
    char footerBuf[24];
    snprintf(footerBuf, sizeof(footerBuf), "Day %d - No events",
             calendarSelDay);
    centerTextFont(footerBuf, 225, C_MUTED, &FreeSans9pt7b);
  }
}

void TaskyUI::drawDayTasks(int day) {
  tft.fillScreen(C_BG);

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
  tft.fillScreen(C_BG);

  // Header with status
  centerTextFont("TASK DETAIL", 28, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 38, 160, C_LINE);

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
    while (remaining.length() > 0 && descY < 200) {
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

  // Footer hint
  centerTextFont("Press to close", 225, C_MUTED, &FreeSans9pt7b);
  tft.setFont();
}

void TaskyUI::drawStreak() {
  tft.fillScreen(C_BG);
  // No frame - cleaner look

  centerTextFont("STREAK", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  // 6 rows x 14 cols - smaller squares for circular display
  int sz = 12;
  int gap = 2;
  int gridW = STREAK_COLS * sz + (STREAK_COLS - 1) * gap;
  int gridH = STREAK_ROWS * sz + (STREAK_ROWS - 1) * gap;

  // Centered grid
  int gx = (W - gridW) / 2;
  int gy = 58;

  // Draw all squares
  for (int r = 0; r < STREAK_ROWS; r++) {
    for (int c = 0; c < STREAK_COLS; c++) {
      int idx = r * STREAK_COLS + c;

      int x = gx + c * (sz + gap);
      int y = gy + r * (sz + gap);

      tft.fillRoundRect(x, y, sz, sz, 2, streakColor(model->streak[idx]));

      // Highlight current day (teal border)
      if (idx == model->currentDayIndex)
        tft.drawRoundRect(x - 1, y - 1, sz + 2, sz + 2, 3, C_TEAL);
      // Highlight cursor position (white border)
      if (idx == model->streakCursor)
        tft.drawRoundRect(x - 2, y - 2, sz + 4, sz + 4, 4, C_TEXT);
    }
    // Yield after each row to prevent watchdog
    yield();
  }

  // Streak statistics
  int currentStreak = model->calculateCurrentStreak();
  int bestStreak = model->cfg.bestStreak;
  int totalActive = model->calculateTotalActiveDays();

  // Current streak (consecutive days)
  char statBuf[32];
  snprintf(statBuf, sizeof(statBuf), "Streak: %d days", currentStreak);
  centerTextFont(statBuf, 168, C_TEAL, &FreeSans12pt7b);

  // Total active days and best record
  snprintf(statBuf, sizeof(statBuf), "Total: %d  Best: %d", totalActive,
           bestStreak);
  centerTextFont(statBuf, 190, C_MUTED, &FreeSans9pt7b);

  // Progress indicator (how full is the streak grid)
  int pct = (totalActive * 100) / STREAK_N;
  snprintf(statBuf, sizeof(statBuf), "%d%% filled", pct);
  centerTextFont(statBuf, 210, C_MUTED, &FreeSans9pt7b);
}

uint16_t TaskyUI::streakColor(uint8_t v) {
  if (v == 0)
    return C_LINE;
  if (v == 1)
    return 0x1B45; // Dark Blue
  if (v == 2)
    return 0x2E47; // Med Blue
  return C_GREEN;
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

void TaskyUI::drawFocusBase(FocusDNS* dns) {
  tft.fillScreen(C_BG);

  // Header
  centerTextFont("FOCUS MODE", 30, C_TEAL, &FreeSansBold12pt7b);
  tft.drawFastHLine(60, 42, 120, C_LINE);

  // Status indicator
  bool active = dns->isFocusModeActive();
  uint16_t statusColor = active ? C_GREEN : C_RED;
  const char* statusText = active ? "ACTIVE" : "INACTIVE";
  centerTextFont(statusText, 64, statusColor, &FreeSansBold12pt7b);

  // Shield icon if active
  if (active) {
    drawFocusIndicator(true);
  }

  // Stats line
  char statsBuf[32];
  snprintf(statsBuf, sizeof(statsBuf), "Blocked: %lu",
           (unsigned long)dns->getBlockedQueryCount());
  centerTextFont(statsBuf, 84, C_MUTED, &FreeSans9pt7b);

  // Separator
  tft.drawFastHLine(20, 92, 200, C_LINE);

  // Domain list
  drawFocusList(dns);

  // Bottom instructions
  tft.drawFastHLine(20, 212, 200, C_LINE);
  centerTextFont("Click:ON/OFF Long:Remove", 230, C_MUTED, nullptr);
}

void TaskyUI::drawFocusList(FocusDNS* dns) {
  // Clear list area
  tft.fillRect(0, 96, 240, 114, C_BG);

  int count = dns->getBlockedCount();
  if (count == 0) {
    centerTextFont("No blocked sites", 145, C_MUTED, &FreeSans9pt7b);
    centerTextFont("Long press to restore", 165, C_MUTED, &FreeSans9pt7b);
    return;
  }

  // Clamp scroll
  int maxVisible = 6;
  if (focusDomainScroll > count - 1) focusDomainScroll = count - 1;
  if (focusDomainScroll < 0) focusDomainScroll = 0;

  // Calculate visible window centered on selection
  int startIdx = focusDomainScroll - maxVisible / 2;
  if (startIdx < 0) startIdx = 0;
  if (startIdx > count - maxVisible) startIdx = count - maxVisible;
  if (startIdx < 0) startIdx = 0;

  int yPos = 98;
  for (int i = startIdx; i < count && i < startIdx + maxVisible; i++) {
    bool selected = (i == focusDomainScroll);
    int itemY = yPos + (i - startIdx) * 19;

    if (selected) {
      tft.fillRect(10, itemY - 2, 220, 18, C_TEAL);
      tft.setTextColor(C_BG);
    } else {
      tft.setTextColor(C_TEXT);
    }

    // Domain name - strip .com/.net suffixes for cleaner display
    const char* domain = dns->getBlockedDomain(i);
    char displayName[24];
    strncpy(displayName, domain, sizeof(displayName) - 1);
    displayName[sizeof(displayName) - 1] = '\0';
    // Strip common suffixes
    char* dot = strstr(displayName, ".com");
    if (dot && strlen(dot) == 4) *dot = '\0';
    dot = strstr(displayName, ".net");
    if (dot && strlen(dot) == 4) *dot = '\0';
    dot = strstr(displayName, ".org");
    if (dot && strlen(dot) == 4) *dot = '\0';

    // Capitalize first letter
    if (displayName[0] >= 'a' && displayName[0] <= 'z') {
      displayName[0] -= 32;
    }

    // Same font size as settings screen
    tft.setFont(&FreeSans12pt7b);
    // Center the name
    int16_t x1, y1;
    uint16_t tw, th;
    tft.getTextBounds(displayName, 0, 0, &x1, &y1, &tw, &th);
    int cx = (W - (int)tw) / 2;
    tft.setCursor(cx, itemY + 13);
    tft.print(displayName);

    tft.setFont();
  }

  // Scroll indicators
  if (startIdx > 0) {
    tft.setTextColor(C_MUTED);
    tft.setFont();
    tft.setCursor(115, 97);
    tft.print("^");
  }
  if (startIdx + maxVisible < count) {
    tft.setTextColor(C_MUTED);
    tft.setFont();
    tft.setCursor(115, 210);
    tft.print("v");
  }
}

void TaskyUI::drawFocus(FocusDNS* dns) {
  drawFocusBase(dns);
}

void TaskyUI::drawAgenda() {
  tft.fillScreen(C_BG);

  centerTextFont("AGENDA", 28, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 38, 160, C_LINE);

  int marginX = 24;
  int yPos = 56;

  // Get current day and day name
  time_t nowTime = time(nullptr);
  struct tm now;
  bool hasTime = (nowTime > 1577836800);
  int currentDay = 1;
  int currentMonth = 0;
  int currentWday = 0;
  if (hasTime) {
    localtime_r(&nowTime, &now);
    currentDay = now.tm_mday;
    currentMonth = now.tm_mon;
    currentWday = now.tm_wday;
  }
  int tomorrowDay = currentDay + 1;
  const int daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (tomorrowDay > daysPerMonth[currentMonth]) tomorrowDay = 1;

  const char* dayNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  int tomorrowWday = (currentWday + 1) % 7;

  // === TODAY section ===
  char todayLabel[24];
  snprintf(todayLabel, sizeof(todayLabel), "Today - %s", dayNames[currentWday]);
  tft.setTextColor(C_TEAL);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setCursor(marginX, yPos);
  tft.print("Today");
  // Day name smaller
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_MUTED);
  tft.setCursor(marginX + 72, yPos);
  tft.print(dayNames[currentWday]);
  yPos += 6;

  // Count today's events from monthEvents
  int todayCount = 0;
  for (int i = 0; i < model->monthEventCount; i++) {
    if (model->monthEvents[i].day == currentDay) todayCount++;
  }

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
  tft.setTextColor(C_ORANGE);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setCursor(marginX, yPos);
  tft.print("Tomorrow");
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_MUTED);
  tft.setCursor(marginX + 112, yPos);
  tft.print(dayNames[tomorrowWday]);
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

void TaskyUI::drawFocusIndicator(bool active) {
  // Shield icon in top right corner indicating blocking is active
  if (!active) return;

  int x = 200;
  int y = 26;

  // Simple shield shape
  uint16_t color = C_RED;

  // Shield outline (pentagon-like shape)
  tft.fillTriangle(x, y - 8, x - 10, y - 4, x + 10, y - 4, color);
  tft.fillRect(x - 10, y - 4, 21, 10, color);
  tft.fillTriangle(x - 10, y + 6, x + 10, y + 6, x, y + 14, color);

  // Inner shield detail (optional - darker center)
  uint16_t innerColor = 0x7800; // Darker red
  tft.fillCircle(x, y, 3, innerColor);
}

void TaskyUI::drawStats() {
  tft.fillScreen(C_BG);
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

  // Sessions completed today
  int todaySessions = model->streak[model->currentDayIndex];
  snprintf(buf, sizeof(buf), "%d sessions today", todaySessions);
  centerTextFont(buf, 175, C_TEAL, &FreeSans12pt7b);
}

#if FEATURE_SPOTIFY
void TaskyUI::drawSpotify(const char* track, const char* artist, uint32_t progressMs,
                          uint32_t durationMs, bool isPlaying, int volume, bool isActive) {
  tft.fillScreen(C_BG);

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
  centerTextFont("Vol: Turn | Click: Play", 230, C_LINE, &FreeSans9pt7b);
  tft.setFont();
}
#endif

#if FEATURE_WEATHER
void TaskyUI::drawWeather(float temp, float feelsLike, uint8_t humidity,
                          uint8_t icon, const char* desc, const char* city, bool celsius) {
  tft.fillScreen(C_BG);

  // No weather data: show time + motivational fallback
#if !TASKY_MODE_DEV
  if (city == nullptr || strlen(city) == 0) {
    centerTextFont("WEATHER", 28, C_TEXT, &FreeSansBold12pt7b);
    tft.drawFastHLine(40, 38, 160, C_LINE);

    // Show current time (useful info)
    int h, m, s;
    model->getClockNow(h, m, s);
    char timeBuf[8];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h, m);
    centerTextFont(timeBuf, 110, C_TEXT, &FreeSansBold24pt7b);

    centerTextFont("EVERY SECOND COUNTS", 145, C_MUTED, &FreeSans9pt7b);
    centerTextFont("Connect WiFi for weather", 220, C_LINE, &FreeSans9pt7b);
    return;
  }
#endif

  // Header
  centerTextFont("WEATHER", 28, C_TEXT, &FreeSansBold12pt7b);
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
  tft.fillScreen(C_BG);

  // State: THINKING animation
  if (state == AGENT_SENDING || state == AGENT_THINKING) {
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

    centerTextFont("Press to cancel", 228, C_MUTED, &FreeSans9pt7b);
    return;
  }

  // State: ERROR (only shows for config issues)
  if (state == AGENT_ERROR) {
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

    centerTextFont("Press to go back", 228, C_MUTED, &FreeSans9pt7b);
    tft.setFont();
    return;
  }

  // State: DONE - show response with styled layout
  if (state == AGENT_DONE && showingResponse && responseLen > 0) {
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
    int maxY = 208;
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
    tft.drawFastHLine(60, 216, 120, C_LINE);
    if (yPos > maxY + lineHeight) {
      centerTextFont("Turn to scroll", 232, C_MUTED, &FreeSans9pt7b);
    } else {
      centerTextFont("Press to go back", 232, C_MUTED, &FreeSans9pt7b);
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
  tft.fillScreen(C_BG);

  centerTextFont("SELECT MODE", 28, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 38, 160, C_LINE);

  // 10 items: AUTO + 9 modes
  static const int TOTAL_MODES = 10;
  static const char* modeLabels[10] = {
    "AUTO", "ENFOQUE", "ARRANQUE", "TIEMPO",
    "CLARIDAD", "ORDEN", "CIERRE", "MOVIMIENTO",
    "GYM", "RECETAS"
  };

  // Determine which item is currently active
  int activeIdx = (currentOverride < 0) ? 0 : (currentOverride + 1);

  // Scrollable list - show 6 items centered on selection
  int visibleCount = 6;
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
    tft.fillTriangle(CX - 5, 224, CX + 5, 224, CX, 229, C_MUTED);
  }

  // Footer
  centerTextFont("Click to select", 225, C_MUTED, &FreeSans9pt7b);
  tft.setFont();
}
#endif

void TaskyUI::drawWifi(bool wifiOn, String ssid, String pass, String ip) {
  tft.fillScreen(C_BG);
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

  // QR fills entire screen
  int scale = 220 / totalModules; // Use almost full 240px
  if (scale < 1)
    scale = 1;

  int dimension = totalModules * scale;
  int startX = (W - dimension) / 2;
  int startY = (H - dimension) / 2; // Perfectly centered

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
  tft.fillScreen(C_BG);

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

// ==================== QUOTES DATA (PROGMEM) ====================
const char quote0[] PROGMEM = "Every second counts.";
const char quote1[] PROGMEM = "Find a way.";
const char quote2[] PROGMEM = "Stay hungry, stay foolish.|Steve Jobs";
const char quote3[] PROGMEM = "Move fast and break things.|Mark Zuckerberg";
const char quote4[] PROGMEM =
    "The only way to do great work is to love what you do.|Steve Jobs";
const char quote5[] PROGMEM = "Done is better than perfect.|Sheryl Sandberg";
const char quote6[] PROGMEM = "Make something people want.|Paul Graham";
const char quote7[] PROGMEM =
    "Ideas are easy. Execution is everything.|John Doerr";
const char quote8[] PROGMEM =
    "The best way to predict the future is to invent it.|Alan Kay";
const char quote9[] PROGMEM =
    "Simplicity is the ultimate sophistication.|Leonardo da Vinci";
const char quote10[] PROGMEM =
    "First, solve the problem. Then, write the code.|John Johnson";
const char quote11[] PROGMEM = "Code is poetry.";
const char quote12[] PROGMEM = "Ship it.|Seth Godin";
const char quote13[] PROGMEM = "Think different.|Apple";
const char quote14[] PROGMEM =
    "What would you attempt if you knew you could not fail?|Robert Schuller";
const char quote15[] PROGMEM = "The obstacle is the way.|Marcus Aurelius";
const char quote16[] PROGMEM = "Memento mori.";
const char quote17[] PROGMEM = "Amor fati.|Nietzsche";
const char quote18[] PROGMEM = "Per aspera ad astra.";
const char quote19[] PROGMEM = "Cogito, ergo sum.|Descartes";
const char quote20[] PROGMEM = "Knowledge is power.|Francis Bacon";
const char quote21[] PROGMEM =
    "The unexamined life is not worth living.|Socrates";
const char quote22[] PROGMEM = "Be the change you wish to see.|Gandhi";
const char quote23[] PROGMEM =
    "Time is the most valuable thing a man can spend.|Theophrastus";
const char quote24[] PROGMEM =
    "Action is the foundational key to all success.|Picasso";
const char quote25[] PROGMEM = "Begin anywhere.|John Cage";
const char quote26[] PROGMEM = "Less is more.|Mies van der Rohe";
const char quote27[] PROGMEM = "Form follows function.|Louis Sullivan";
const char quote28[] PROGMEM =
    "Make it work, make it right, make it fast.|Kent Beck";
const char quote29[] PROGMEM = "Iteration is the mother of progress.";

const char *const quotes[] PROGMEM = {
    quote0,  quote1,  quote2,  quote3,  quote4,  quote5,  quote6,  quote7,
    quote8,  quote9,  quote10, quote11, quote12, quote13, quote14, quote15,
    quote16, quote17, quote18, quote19, quote20, quote21, quote22, quote23,
    quote24, quote25, quote26, quote27, quote28, quote29};

#define NUM_QUOTES 30

void TaskyUI::drawQuotes() {
  tft.fillScreen(0x0000); // Pure black background

  // Read quote from PROGMEM
  char buffer[128];
  strcpy_P(buffer, (char *)pgm_read_ptr(&quotes[quoteIndex]));

  String fullQuote = String(buffer);

  // Split quote and author (separated by |)
  String quoteText = fullQuote;
  String author = "";

  int pipePos = fullQuote.indexOf('|');
  if (pipePos > 0) {
    quoteText = fullQuote.substring(0, pipePos);
    author = fullQuote.substring(pipePos + 1);
  }

  // Word wrap the quote for multi-line display
  // Max ~20 chars per line for 12pt font
  const int maxLineLen = 18;
  const int maxLines = 4;
  String lines[maxLines];
  int numLines = 0;

  String remaining = quoteText;
  while (remaining.length() > 0 && numLines < maxLines) {
    if (remaining.length() <= maxLineLen) {
      lines[numLines++] = remaining;
      break;
    }

    // Find space to break at
    int breakAt = maxLineLen;
    for (int i = maxLineLen; i > 0; i--) {
      if (remaining[i] == ' ') {
        breakAt = i;
        break;
      }
    }

    lines[numLines++] = remaining.substring(0, breakAt);
    remaining = remaining.substring(breakAt);
    remaining.trim();
  }

  // Calculate vertical centering
  int lineHeight = 28;
  int totalHeight = numLines * lineHeight + (author.length() > 0 ? 30 : 0);
  int startY = (240 - totalHeight) / 2 + 20;

  // Draw quote lines - OP-1 style (white on black)
  tft.setFont(&FreeSansBold12pt7b);
  tft.setTextColor(0xFFFF); // Pure white

  for (int i = 0; i < numLines; i++) {
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(lines[i], 0, 0, &x1, &y1, &w, &h);
    int x = (240 - w) / 2;
    tft.setCursor(x, startY + i * lineHeight);
    tft.print(lines[i]);
  }

  // Draw author - smaller, muted
  if (author.length() > 0) {
    int authorY = startY + numLines * lineHeight + 20;
    tft.setFont(&FreeSans9pt7b);
    tft.setTextColor(0x7BEF); // Muted gray

    String authorLine = "- " + author;
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(authorLine, 0, 0, &x1, &y1, &w, &h);
    int x = (240 - w) / 2;
    tft.setCursor(x, authorY);
    tft.print(authorLine);
  }

  tft.setFont();

  // Store time for auto-advance
  lastQuoteChange = millis();
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
