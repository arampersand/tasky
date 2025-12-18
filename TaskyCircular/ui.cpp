#include "ui.h"

// Buzzer constants
extern uint32_t lastUiBeepAt;
extern uint32_t uiBeepOffAt;
extern void buzzerTone(uint16_t hz);
extern void buzzerOff();

// --- Constants ---
static const int W = 240, H = 240, CX = 120, CY = 120;
static const int PWM_FREQ = 5000;
static const int PWM_RES = 8;

static const uint16_t C_FLASH = 0x39E7;
static const uint16_t C_FLASH2 = 0x1082;
static const int PROG_SEG_N = 12;

TaskyUI::TaskyUI(TaskyModel *m, int8_t cs, int8_t dc, int8_t rst)
    : model(m), tft(cs, dc, rst), _rst(rst) {}

void TaskyUI::init() {
  // Explicitly reset the display using the stored pin
  pinMode(_rst, OUTPUT);
  digitalWrite(_rst, LOW);
  delay(10);
  digitalWrite(_rst, HIGH);
  delay(150);

#if DISPLAY_CIRCULAR
  // GC9A01A circular display init
  tft.begin();
  tft.setRotation(0);
  tft.invertDisplay(true); // Fix for washed out colors on some GC9A01
#else
  // ST7789 square display init (240x240)
  tft.init(240, 240);
  tft.setRotation(2);
  tft.invertDisplay(true); // Most ST7789 need inversion
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

// Draw Frame Abstraction
void TaskyUI::drawFrame() {
#if DISPLAY_CIRCULAR
  // Circular frame: rim logic
  tft.drawCircle(CX, CY, 119, C_LINE);
#else
  // Square display: no frame on non-timer screens
  // Timer screen draws its own progress dots
#endif
}

// Navigation dots indicator
void TaskyUI::drawNavDots(Screen current) {
  // Navigation order: Timer, Tasks, Calendar, Streak, Stats, Settings, QR, WiFi
  const int navOrder[] = {SCR_TIMER, SCR_TASKS, SCR_CALENDAR, SCR_STREAK, 
                          SCR_STATS, SCR_SETTINGS, SCR_QR, SCR_WIFI_INFO};
  const int numDots = 8;
  const int dotRadius = 3;
  const int dotSpacing = 12;
  
  // Center the dots horizontally
  int totalWidth = (numDots - 1) * dotSpacing;
  int startX = CX - totalWidth / 2;
  
#if DISPLAY_CIRCULAR
  int y = 225;  // Near bottom of circular display
#else
  int y = 210;  // Above progress dots on square display
#endif
  
  for (int i = 0; i < numDots; i++) {
    int x = startX + i * dotSpacing;
    uint16_t color = (navOrder[i] == current) ? C_TEAL : C_LINE;
    tft.fillCircle(x, y, dotRadius, color);
  }
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
  flashUntilMs = now + (strong ? 140 : 90);
}

void TaskyUI::drawFlashIfNeeded() {
  if (!flashOn)
    return;
  if (millis() > flashUntilMs) {
#if DISPLAY_CIRCULAR
    tft.drawCircle(CX, CY, 119, C_LINE);
#else
    tft.drawRoundRect(10, 10, 220, 220, 16, C_LINE);
#endif
    flashOn = false;
    return;
  }
#if DISPLAY_CIRCULAR
  tft.drawCircle(CX, CY, 119, C_FLASH);
#else
  tft.drawRoundRect(10, 10, 220, 220, 16, C_FLASH);
#endif
}

uint16_t TaskyUI::accentColor() {
  return (model->phase == PHASE_WORK) ? C_TEAL : C_ORANGE;
}

// Helper to get color based on progress for ring
uint16_t getRingColor(float pct) {
  // pct 0.0 (empty) to 1.0 (full)
  // Logic: 1.0 -> 0.5 (Blue/Teal), 0.5 -> 0.2 (Yellow), 0.2 -> 0.0 (Red)
  if (pct > 0.5)
    return 0x2E57; // Teal/Blue-ish
  if (pct > 0.2)
    return 0xFD20; // Orange/Yellow
  return 0xF800;   // Red
}

void TaskyUI::drawProgressSegments() {
  uint32_t dur = model->phaseDurationMs();
  uint32_t rem = model->remainingMs();

  if (dur == 0)
    dur = 1;

  float pct = (float)rem / (float)dur;
  int totalDots = 60;
  int dotsOn = (int)(pct * totalDots);
  if (dotsOn < 0)
    dotsOn = 0;
  if (dotsOn > totalDots)
    dotsOn = totalDots;

  // No cache - always redraw to prevent blank screen after screen clear

#if DISPLAY_CIRCULAR
  int r = 114;
  for (int i = 0; i < totalDots; i++) {
    // Reverse direction: Start from top (-90), go Left (Counter-Clockwise)
    // Angle = -90 - (360/total) * i
    float angle = -90.0 - (360.0 / totalDots) * i;
    float rad = angle * 3.14159 / 180.0;

    uint16_t color = C_LINE;
    if (i < dotsOn) {
      color = getRingColor(pct);
    }

    int x = CX + (int)(cos(rad) * r);
    int y = CY + (int)(sin(rad) * r);

    if (i < dotsOn)
      tft.fillCircle(x, y, 2, color);
    else
      tft.fillCircle(x, y, 1, C_LINE);
  }
#else
  // Square display: dots around 4 edges following frame margin
  // 60 dots total: 15 per edge
  // Using 10px margin from edge (same as old frame rectangle)
  int margin = 10;
  int edgeLen = 220;    // Frame was 220x220
  int spacing = edgeLen / 15;  // ~14.6px between dots
  
  uint16_t activeColor = getRingColor(pct);
  
  for (int i = 0; i < totalDots; i++) {
    int px, py;
    int edge = i / 15;  // 0=top, 1=right, 2=bottom, 3=left
    int pos = i % 15;   // Position within edge
    
    switch (edge) {
      case 0:  // Top edge (left to right)
        px = margin + pos * spacing;
        py = margin;
        break;
      case 1:  // Right edge (top to bottom)
        px = W - margin;
        py = margin + pos * spacing;
        break;
      case 2:  // Bottom edge (right to left)
        px = W - margin - pos * spacing;
        py = H - margin;
        break;
      case 3:  // Left edge (bottom to top)
        px = margin;
        py = H - margin - pos * spacing;
        break;
    }
    
    if (i < dotsOn)
      tft.fillCircle(px, py, 2, activeColor);
    else
      tft.fillCircle(px, py, 1, C_LINE);
  }
#endif
}

void TaskyUI::drawTimerBase() {
  tft.fillScreen(C_BG);
  flashOn = false;
  
  drawFrame();

  // Phase indicator
  uint16_t dot = accentColor();
  tft.fillCircle(CX, 22, 3, dot);

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

  // Clear previous text area strictly?
  // circle is 120,120. text width ~150, height ~50.
  // Rect: x=45, y=95, w=150, h=50
  tft.fillRect(CX - 85, CY - 35, 170, 70, C_BG);

  tft.setCursor(cx, cy);
  tft.print(buf);
  tft.setFont();

  // Status (Paused/Done)
  // Clear status area
  tft.fillRect(40, 160, 160, 30, C_BG);

  if (model->st.finished) {
    centerTextFont("DONE", 180, C_RED, &FreeSansBold12pt7b);
  } else if (!model->st.running) {
    centerTextFont("PAUSED", 180, C_MUTED, &FreeSansBold12pt7b);
  }

  drawProgressSegments();

  // Small bottom dot
  tft.fillCircle(CX, 216, 2, C_LINE);
}

void TaskyUI::drawSettings() {
  tft.fillScreen(C_BG);
  drawFrame();
  
  centerTextFont("SETTINGS", 32, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(30, 46, 180, C_LINE);

  // Settings: Work, Break, Sound, Bright, LED, Theme (0-5)
  const char *labels[6] = {"Work", "Break", "Sound", "Bright", "LED", "Theme"};
  String values[6];
  values[0] = String(model->cfg.workMin) + "m";
  values[1] = String(model->cfg.breakMin) + "m";
  values[2] = model->cfg.soundOn ? "ON" : "OFF";
  values[3] = String((model->cfg.screenBright * 100) / 255) + "%";
  values[4] = String((model->cfg.ledBright * 100) / 255) + "%";
  values[5] = model->cfg.darkMode ? "DARK" : "LIGHT";

  int yStart = 72;
  int rowH = 28;  // Bigger rows

  // Draw all items
  for (int i = 0; i < 6; i++) {
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
    
    tft.setTextColor(labelColor);
    tft.setFont(&FreeSans12pt7b);  // Bigger font
    tft.setCursor(28, baseline);
    tft.print(labels[i]);
    rightTextFont(values[i], 212, baseline, valueColor, &FreeSans12pt7b);
  }

}

// End of ui.cpp or similar valid location. Since TargetContent was empty
// previously, I will target the end of file or a known specific function to
// append/insert. Let's target the end of drawTasks or similar.
void TaskyUI::drawTasks() {
  tft.fillScreen(C_BG);
  drawFrame();
  
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
    centerTextFont("No tasks yet", 95, C_TEXT, &FreeSansBold12pt7b);
    centerTextFont("Scan the QR code", 125, C_MUTED, &FreeSans9pt7b);
    centerTextFont("to add your tasks", 145, C_MUTED, &FreeSans9pt7b);
    
    // QR icon hint - centered
    int qrX = 108, qrY = 165;
    tft.drawRect(qrX, qrY, 24, 24, C_TEAL);
    tft.fillRect(qrX + 3, qrY + 3, 6, 6, C_TEAL);
    tft.fillRect(qrX + 15, qrY + 3, 6, 6, C_TEAL);
    tft.fillRect(qrX + 3, qrY + 15, 6, 6, C_TEAL);
    tft.fillRect(qrX + 13, qrY + 13, 5, 5, C_TEAL);
    return;
  }

  // Tasks layout - lowered for better spacing
  int yBase = 80;
  int rowH = 42;
  
  // Draw cursor highlight for selected item
  int cursorY = yBase + tasksSel * rowH;
  tft.fillRoundRect(14, cursorY - 16, 212, 30, 10, 0x0841);
  tft.drawRoundRect(14, cursorY - 16, 212, 30, 10, C_TEAL);

  // Draw all tasks
  for (int i = 0; i < 3; i++) {
    int baseline = yBase + i * rowH;

    // Checkbox
    uint16_t boxC = model->taskDone[i] ? C_GREEN : C_MUTED;
    tft.drawRoundRect(22, baseline - 8, 16, 16, 3, boxC);
    if (model->taskDone[i]) {
      // Checkmark
      tft.drawLine(25, baseline - 1, 28, baseline + 3, C_GREEN);
      tft.drawLine(28, baseline + 3, 35, baseline - 5, C_GREEN);
    }

    // Task text
    tft.setTextColor(model->taskDone[i] ? C_MUTED : C_TEXT);
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(44, baseline);
    String t = model->tasks[i];
    if (t == "Empty") t = "---";
    if (t.length() > 14)
      t = t.substring(0, 14) + "..";
    tft.print(t);
    tft.setFont();
  }
}

void TaskyUI::drawCalendar() {
  tft.fillScreen(C_BG);
  drawFrame();
  
  centerTextFont("CALENDAR", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  // Check if calendar is configured
  if (model->cfg.calendarUrl.length() == 0) {
    // No calendar configured - centered layout
    centerTextFont("No Calendar", 90, C_TEXT, &FreeSansBold12pt7b);
    centerTextFont("Connect via WiFi", 118, C_MUTED, &FreeSans9pt7b);
    centerTextFont("and add your", 136, C_MUTED, &FreeSans9pt7b);
    centerTextFont("iCal URL in settings", 154, C_MUTED, &FreeSans9pt7b);
    
    // WiFi icon hint - more space from text
    tft.fillCircle(120, 180, 3, C_TEAL);
    tft.drawCircle(120, 180, 8, C_TEAL);
    tft.drawCircle(120, 180, 14, C_TEAL);
    
    centerTextFont("Scan QR to setup", 215, C_MUTED, &FreeSans9pt7b);
    return;
  }

  // Show next event info
  centerTextFont("NEXT EVENT", 65, C_MUTED, &FreeSans9pt7b);

  // Event Time - prominent display
  tft.setTextColor(C_TEAL);
  tft.setFont(&FreeSansBold18pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(model->cfg.nextEventTime, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(120 - w / 2, 105);
  tft.print(model->cfg.nextEventTime);

  // Event Title
  tft.setTextColor(C_TEXT);
  tft.setFont(&FreeSans12pt7b);

  String title = model->cfg.nextEventSummary;
  if (title.length() > 18)
    title = title.substring(0, 18) + "..";

  tft.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(120 - w / 2, 145);
  tft.print(title);

  // Separator
  tft.drawFastHLine(50, 160, 140, C_LINE);

  // Instructions - moved up for circular display
  centerTextFont("Syncs from your", 180, C_MUTED, &FreeSans9pt7b);
  centerTextFont("iCal calendar", 198, C_MUTED, &FreeSans9pt7b);
}

void TaskyUI::drawTaskDetail(int index) {
  if (index < 0 || index >= 3)
    return;
  String full = model->tasks[index];
  if (full.length() == 0)
    return;

  // Overlay style
  tft.fillRoundRect(20, 40, 200, 160, 10, C_BG);
  tft.drawRoundRect(20, 40, 200, 160, 10, C_TEAL);

  tft.setTextColor(C_TEXT);
  tft.setFont(&FreeSansBold12pt7b);
  tft.setCursor(30, 70);
  tft.print("Task Details:");

  tft.setFont(&FreeSans9pt7b);

  // Simple word wrap or multi-line
  // For now simple print, improve later if user complains
  int y = 100;
  int start = 0;
  while (start < full.length() && y < 190) {
    String line = full.substring(start);
    if (line.length() > 20) {
      // Find space?
      int sp = line.lastIndexOf(' ', 20);
      if (sp > 0)
        line = line.substring(0, sp);
      else
        line = line.substring(0, 20);
    }
    tft.setCursor(30, y);
    tft.print(line);
    start += line.length();
    while (start < full.length() && full[start] == ' ')
      start++;
    y += 20;
  }
}

void TaskyUI::drawStreak() {
  tft.fillScreen(C_BG);
  drawFrame();
  
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
  }

  // Streak statistics
  int currentStreak = model->calculateCurrentStreak();
  int bestStreak = model->cfg.bestStreak;
  
  char statBuf[32];
  snprintf(statBuf, sizeof(statBuf), "Now: %d  Best: %d", currentStreak, bestStreak);
  centerTextFont(statBuf, 168, C_TEAL, &FreeSans9pt7b);
  centerTextFont("days", 184, C_MUTED, &FreeSans9pt7b);

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

void TaskyUI::drawStats() {
  tft.fillScreen(C_BG);
  drawFrame();
  
  centerTextFont("STATS", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  // Focus Time - Main stat
  uint32_t mins = model->cfg.totalFocusMin;
  int hours = mins / 60;
  int m = mins % 60;

  char buf[32];
  snprintf(buf, sizeof(buf), "%dh %02dm", hours, m);

  centerTextFont(buf, 100, C_TEXT, &FreeSansBold18pt7b);
  centerTextFont("Total Focus Time", 126, C_MUTED, &FreeSans9pt7b);

  // Sessions completed today (calculated from streak)
  int todaySessions = model->streak[model->currentDayIndex];
  snprintf(buf, sizeof(buf), "%d sessions today", todaySessions);
  centerTextFont(buf, 165, C_TEAL, &FreeSans12pt7b);

  // Motivational message
  if (todaySessions == 0) {
    centerTextFont("Start a session!", 188, C_MUTED, &FreeSans9pt7b);
  } else if (todaySessions >= 3) {
    centerTextFont("Great work!", 188, C_GREEN, &FreeSans9pt7b);
  } else {
    centerTextFont("Keep going!", 188, C_MUTED, &FreeSans9pt7b);
  }
}

void TaskyUI::drawWifi(bool wifiOn, String ssid, String pass, String ip) {
  tft.fillScreen(C_BG);
  drawFrame();

  centerTextFont("WIFI", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  // SSID row
  tft.setTextColor(C_MUTED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(25, 75);
  tft.print("SSID");
  
  tft.setTextColor(C_TEAL);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(25, 97);
  tft.print(ssid);

  // PASS row
  tft.setTextColor(C_MUTED);
  tft.setFont(&FreeSans9pt7b);
  tft.setCursor(25, 122);
  tft.print("PASS");
  
  tft.setTextColor(C_TEAL);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(25, 144);
  tft.print(pass);

  centerTextFont(ip.c_str(), 175, C_SOFT, &FreeSans9pt7b);
  tft.setFont();
}

void TaskyUI::drawQRFull(String ssid, String pass) {
  tft.fillScreen(0xFFFF);

  String payload = "WIFI:S:" + ssid + ";T:WPA;P:" + pass + ";;";
  qrcodegen::QrCode qr =
      qrcodegen::QrCode::encodeText(payload.c_str(), ECC_LOW);

  int size = qr.getSize();
  int border = 1;
  int totalModules = size + (border * 2);

  // QR fills entire screen
  int scale = 220 / totalModules;  // Use almost full 240px
  if (scale < 1)
    scale = 1;

  int dimension = totalModules * scale;
  int startX = (W - dimension) / 2;
  int startY = (H - dimension) / 2;  // Perfectly centered

  for (int y = -border; y < size + border; y++) {
    for (int x = -border; x < size + border; x++) {
      bool isBlack = false;
      if (x >= 0 && x < size && y >= 0 && y < size) {
        if (qr.getModule(x, y)) {
          isBlack = true;
        }
      }
      uint16_t color = isBlack ? C_TEAL : 0xFFFF;  // Teal QR for branding
      tft.fillRect(startX + (x + border) * scale, startY + (y + border) * scale,
                   scale, scale, color);
    }
  }
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
    cumX += lw + 2;  // Letter spacing
  }
  
  // Fade colors: dark to bright
  uint16_t fadeSteps[] = {0x0841, 0x1082, 0x18C3, 0x2104, 0x3186, 0x4208, 0x52AA, 0x6B4D, 0x8410, 0xAD55, 0xD69A, 0xFFFF};
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
    }
    
    // Brief pause between letters
    delay(80);
  }
  
  // Accent line under logo
  delay(200);
  for (int w = 0; w <= 60; w += 4) {
    tft.drawFastHLine(120 - w/2, 145, w, C_TEAL);
    delay(10);
  }
  
  // Tagline fades in
  delay(200);
  uint16_t tagFades[] = {0x2104, 0x4208, 0x630C, C_MUTED};
  for (int f = 0; f < 4; f++) {
    centerTextFont("Focus. Flow. Finish.", 175, tagFades[f], &FreeSans9pt7b);
    delay(50);
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
