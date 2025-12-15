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

  // GC9A01A Init
  tft.begin();
  // tft.setSPISpeed(4000000); // Conservative speed if needed
  // tft.setSPISpeed(
  //     40000000); // Attempt high speed if SoftSPI supports it or ignored
#if DISPLAY_CIRCULAR
  tft.setRotation(0);
  tft.invertDisplay(
      true); // Fix for washed out colors / ghosting on some GC9A01
#else
  tft.setRotation(2); // From original code
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
  // Clear screen properly with current BG
  tft.fillScreen(C_BG);
  baseDrawn = false;
  settingsEditing = false;
  clockEditing = false;
  streakEditing = false;
}

// Draw Frame Abstraction
void TaskyUI::drawFrame() {
#if DISPLAY_CIRCULAR
  // Circular frame: rim logic
  tft.drawCircle(CX, CY, 119, C_LINE);
#else
  tft.drawRoundRect(10, 10, 220, 220, 16, C_LINE);
#endif
}

void TaskyUI::uiTick(bool strong) {
  uint32_t now = millis();
  if (model->cfg.soundOn && (now - lastUiBeepAt > 60)) {
    lastUiBeepAt = now;
    buzzerTone(strong ? 2600 : 2200);
    uiBeepOffAt = now + 10;
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
    dur = 1; // safety

  float pct = (float)rem / (float)dur;
  int totalDots = 60; // 60 dots ring
  int dotsOn = (int)(pct * totalDots);
  if (dotsOn < 0)
    dotsOn = 0;
  if (dotsOn > totalDots)
    dotsOn = totalDots;

  // We redraw ONLY if changed?
  // For smooth ring, we might just redraw all unless optimized.
  // Optimization: only redraw if dotsOn changed.

  static int lastDotsOn = -1;
  // Force redraw if base redraw happened
  if (lastDotsOn == dotsOn && baseDrawn && lastDotsOn != -1)
    return;
  lastDotsOn = dotsOn;

#if DISPLAY_CIRCULAR
  int r = 114; // Radius
  // Draw all 60 dots
  for (int i = 0; i < totalDots; i++) {
    // Angle: Start from top (-90 deg), go clockwise
    float angle = (360.0 / totalDots) * i - 90;
    float rad = angle * 3.14159 / 180.0;

    // Determine color
    // Active dots get color, inactive get muted/line
    // As time passes, dots turn OFF (become muted)
    // "Countdown": dots disappear.
    // Or "Progress": dots fill. Protocol says "Countdown" usually.
    // User said "ring of dots... blue yellow red when about to finish"
    // So existing dots should have the color.

    uint16_t color = C_LINE;
    if (i < dotsOn) {
      // Color depends on GLOBAL percentage, not per dot
      color = getRingColor(pct);
    }

    // Draw dot (small circle or thick line)
    int x = CX + (int)(cos(rad) * r);
    int y = CY + (int)(sin(rad) * r);

    if (i < dotsOn)
      tft.fillCircle(x, y, 2, color); // Active dot slightly larger?
    else
      tft.fillCircle(x, y, 1, C_LINE); // Inactive dot small
  }
#else
  // Square fallback (simplified)
  int w = 220;
  int h = 6;
  int x = 10;
  int y = 220;
  tft.drawRect(x, y, w, h, C_LINE);
  tft.fillRect(x, y, (int)(w * pct), h, getRingColor(pct));
#endif
}

void TaskyUI::drawTimerBase() {
  tft.fillScreen(C_BG);
  flashOn = false;
  drawFrame();

  // Phase dot
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

  // Subtext hint
  centerTextFont("Click to continue", 160, C_MUTED, &FreeSans9pt7b);
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
  centerTextFont("SETTINGS", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  const char *labels[6] = {"Work", "Break", "Sound", "Bright", "LED", "Theme"};
  String values[6];
  values[0] = String(model->cfg.workMin) + "m";
  values[1] = String(model->cfg.breakMin) + "m";
  values[2] = model->cfg.soundOn ? "ON" : "OFF";
  values[3] = String((model->cfg.screenBright * 100) / 255) + "%";
  values[4] = String((model->cfg.ledBright * 100) / 255) + "%";
  values[5] = model->cfg.darkMode ? "DARK" : "LIGHT";

  int y0 = 74;
  int rowH = 30;

  // Safe margins for circular?
  // Center is wide, edges narrow.
  // 30px row height. Y range 74 to 74+5*30 = 224.
  // 224 is near bottom edge. In circle R=120, at Y=224 (offset 104), X width is
  // sqrt(120^2-104^2)*2 = sqrt(14400-10816)*2 = sqrt(3584)*2 = 60*2 = 120. Our
  // content might be wider. Labels + Values. We might need to contract the list
  // or scroll. For this refactor, we keep it simple. If it clips, it clips
  // (user said "adapt to circular"). Maybe shift Y up a bit? y0=60.

#if DISPLAY_CIRCULAR
  y0 = 60;   // Shift up
  rowH = 26; // Condense slightly
#endif

  for (int i = 0; i < 6; i++) {
    int baseline = y0 + i * rowH;
    bool sel = (i == settingsSel);

    if (sel) {
      uint16_t bg = settingsEditing ? 0x0841 : 0x1082;
      // Circular list visual: maybe rounded rect that fits
      int w = 212;
      if (baseline > 190)
        w = 140; // Narrower at bottom
      int x = (W - w) / 2;
      tft.fillRoundRect(x, baseline - 18, w, 22, 10, bg);
      tft.drawRoundRect(x, baseline - 18, w, 22, 10,
                        settingsEditing ? C_TEAL : C_LINE);
    }

    tft.setTextColor(C_TEXT);
    tft.setFont(&FreeSans12pt7b);

    // Simple layout: Label Left, Value Right
    // Adjust margins for circular
    int xL = 22;
    int xR = 218;

    // Quick heuristic for circular width at Y
    // .. skipping complex math, just indentation if near edges
    if (baseline > 200 || baseline < 40) {
      xL = 50;
      xR = 190;
    }

    tft.setCursor(xL, baseline);
    tft.print(labels[i]);

    rightTextFont(values[i], xR, baseline, sel ? C_SOFT : C_TEXT,
                  &FreeSans12pt7b);
    tft.setFont();
  }
}

void TaskyUI::drawTasks() {
  tft.fillScreen(C_BG);
  centerTextFont("TASKS", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  int y0 = 80;
  int rowH = 44;

  for (int i = 0; i < 3; i++) {
    int baseline = y0 + i * rowH;
    bool sel = (i == tasksSel);

    if (sel) {
      tft.fillRoundRect(12, baseline - 22, 216, 34, 12, 0x0841);
      tft.drawRoundRect(12, baseline - 22, 216, 34, 12, C_TEAL);
    }

    uint16_t boxC = model->taskDone[i] ? C_GREEN : C_MUTED;
    tft.drawRoundRect(22, baseline - 10, 18, 18, 4, boxC);
    if (model->taskDone[i]) {
      tft.drawLine(26, baseline - 2, 30, baseline + 4, C_GREEN);
      tft.drawLine(30, baseline + 4, 38, baseline - 6, C_GREEN);
    }

    tft.setTextColor(model->taskDone[i] ? C_MUTED : C_TEXT);
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(46, baseline);
    String t = model->tasks[i];
    if (t.length() > 16)
      t = t.substring(0, 16) + "...";
    tft.print(t);
    tft.setFont();
  }
}

void TaskyUI::drawStreak() {
  tft.fillScreen(C_BG);
  centerTextFont("STREAK", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  int sz = 10;
  int gap = 3;
  int gridW = STREAK_COLS * sz + (STREAK_COLS - 1) * gap;
  // int gridH = STREAK_ROWS * sz + (STREAK_ROWS - 1) * gap;

  int gx = (W - gridW) / 2;
  int gy = 70;

  for (int r = 0; r < STREAK_ROWS; r++) {
    for (int c = 0; c < STREAK_COLS; c++) {
      int idx = r * STREAK_COLS + c;
      int x = gx + c * (sz + gap);
      int y = gy + r * (sz + gap);

      tft.fillRoundRect(x, y, sz, sz, 2, streakColor(model->streak[idx]));

      if (idx == model->currentDayIndex)
        tft.drawRoundRect(x - 1, y - 1, sz + 2, sz + 2, 3, C_TEAL);
      if (idx == model->streakCursor)
        tft.drawRoundRect(x - 2, y - 2, sz + 4, sz + 4, 4, C_TEXT);
    }
  }
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

void TaskyUI::drawClock() {
  tft.fillScreen(C_BG);
  centerTextFont("CLOCK", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  int hh, mm, ss;
  model->getClockNow(hh, mm, ss);
  char buf[10];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hh, mm, ss);

  tft.setTextColor(C_TEXT);
  tft.setFont(&FreeSansBold18pt7b);
  int w = 0, h = 0, x1 = 0, y1 = 0;
  // Use text bounds if needed, or approx centering
  // centerTextFont handles it
  centerTextFont(buf, 134, C_TEXT, &FreeSansBold18pt7b);

  centerTextFont("EVERY SECOND COUNTS", 168, C_MUTED, &FreeSans9pt7b);

  if (clockEditing) {
    tft.fillRoundRect(82, 184, 76, 18, 8, 0x0841);
    tft.drawRoundRect(82, 184, 76, 18, 8, C_TEAL);
    const char *txt = (clockField == 0)
                          ? "EDIT H"
                          : ((clockField == 1) ? "EDIT M" : "EDIT S");
    centerTextFont(txt, 198, C_SOFT, &FreeSans9pt7b);
  }
}

void TaskyUI::drawWifi(bool wifiOn, String ssid, String pass, String ip) {
  tft.fillScreen(C_BG);
  centerTextFont("WIFI", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  tft.setTextColor(C_TEXT);
  tft.setFont(&FreeSans12pt7b);
  tft.setCursor(18, 84);
  tft.print("SSID:");
  tft.setCursor(80, 84);
  tft.print(ssid);

  tft.setCursor(18, 116);
  tft.print("PASS:");
  tft.setCursor(80, 116);
  tft.print(pass);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_MUTED);
  centerTextFont("Click to show QR", 170, C_MUTED, &FreeSans9pt7b);

  tft.setTextColor(C_SOFT);
  centerTextFont(ip.c_str(), 204, C_SOFT, &FreeSans9pt7b);
  tft.setFont();
}

void TaskyUI::drawQRFull(String ssid, String pass) {
  // Use pure white for QR readability
  tft.fillScreen(0xFFFF);

  // Construct WiFi payload: WIFI:S:SSID;T:WPA;P:PASSWORD;;
  // Note: escaping special chars in SSID/PASS might be needed but for now
  // simple concatenation.
  String payload = "WIFI:S:" + ssid + ";T:WPA;P:" + pass + ";;";

  // Use Nayuki's C++ API directly
  // We use Ecc::LOW for best density/size trade-off on small screen
  qrcodegen::QrCode qr =
      qrcodegen::QrCode::encodeText(payload.c_str(), ECC_LOW);

  int size = qr.getSize();

  // Calculate scaling to fit 240x240 circular screen
  // The safe square inside 240 circle is ~170x170
  // We add a quiet zone (border) of at least 2 modules
  int border = 2; // Quiet zone modules
  int totalModules = size + (border * 2);

  int scale = 168 / totalModules;
  if (scale < 1)
    scale = 1;

  int dimension = totalModules * scale;
  int startX = (W - dimension) / 2;
  int startY = (H - dimension) / 2;

  // Draw QR
  for (int y = -border; y < size + border; y++) {
    for (int x = -border; x < size + border; x++) {
      // Determine color: Black (true) or White (false)
      // Border area is always white
      bool isBlack = false;
      if (x >= 0 && x < size && y >= 0 && y < size) {
        if (qr.getModule(x, y)) {
          isBlack = true;
        }
      }

      uint16_t color = isBlack ? 0x0000 : 0xFFFF;
      tft.fillRect(startX + (x + border) * scale, startY + (y + border) * scale,
                   scale, scale, color);
    }
  }
}

void TaskyUI::drawSplashTasky() {
  tft.fillScreen(C_SPLASH_BG);
  centerTextFont("Tasky", 128, C_SPLASH_TX, &FreeSansBold24pt7b);
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
