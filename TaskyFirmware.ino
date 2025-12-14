// Tasky - Pomodoro Pro (XIAO ESP32C3 + 240x240 SQUARE TFT)
// FINAL SQUARE VERSION (matches the circular firmware features, adapted for
// square) Screens: TIMER / SETTINGS / TASKS / STREAK / CLOCK / WIFI / QR
// Encoder UX:
//   - TIMER: Rotate always adjusts minutes by +5/-5 (no edit mode). Click
//   toggles Start/Pause.
//           When finished: click -> next phase.
//   - SETTINGS: Rotate navigates. Click enters edit. Rotate changes value.
//   Click confirms.
//   - TASKS: Rotate selects. Click toggles done.
//   - STREAK: Rotate moves cursor left->right (top-left to right, row-major).
//   Click edits "today" cell value 0..3.
//   - CLOCK: Shows HH:MM:SS + tiny "EVERY SECOND COUNTS". Click to edit
//   H->M->S.
//   - WIFI: Shows SSID/PASS + IP. Click -> QR screen (full screen QR).
//   - QR: Full screen QR to http://192.168.4.1 (no title). Click -> back to
//   WIFI.
//
// NOTE: Screen rotation fixed (square screen was upside-down): setRotation(2)

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <math.h>
#include <pgmspace.h>
#include <qrcode.h>

// Fonts (cleaner UI)
#include <DNSServer.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
DNSServer dnsServer;
const byte DNS_PORT = 53;

// -------------------- Enums --------------------
enum Screen {
  SCR_TIMER = 0,
  SCR_TASKS,
  SCR_STREAK,
  SCR_CLOCK,
  SCR_WIFI_INFO,
  SCR_SETTINGS,
  SCR_WIFI_QR // QR aparte (no entra en el ciclo)
};

enum Phase { PHASE_WORK, PHASE_BREAK };
enum EncClickType { ENC_NONE, ENC_SHORT, ENC_LONG };

// -------------------- Global state --------------------
Screen screen = SCR_TIMER;
Phase phase = PHASE_WORK;

WebServer server(80);
bool wifiOn = false;

String apSSID = "Tasky";
String apPass = "EverySecondCounts";

// -------------------- Pins (yours) --------------------
#define TFT_BL 2    // D0 - Backlight
#define TFT_CS 3    // D1
#define TFT_DC 4    // D2
#define TFT_RST 5   // D3
#define TFT_MOSI 10 // D10
#define TFT_SCK 8   // D8

#define ENCODER_CLK 6 // D4
#define ENCODER_DT 7  // D5
#define ENCODER_SW 21 // D6 (encoder button)
#define QR_WHITE 0xFFFF
#define QR_BLACK 0x0000

#define MODO_BTN 20 // D7 (screen switch)

#define LED_PIN 9
#define BUZZER_PIN                                                             \
  1 // NOTE: if LED+BUZZER are physically in series, software can't separate
    // them.

// -------------------- Display (SQUARE) --------------------
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// -------------------- Storage --------------------
Preferences pref;

// -------------------- Web --------------------

// -------------------- UI constants --------------------
static const int W = 240, H = 240, CX = 120, CY = 120;

// -------------------- Palette (Nothing-ish / clean) --------------------
#define C_BG 0x0000
#define C_TEXT 0xFFFF
#define C_MUTED 0x7BEF
#define C_LINE 0x3186
#define C_SOFT 0xE71C
#define C_TEAL 0x2E57
#define C_GREEN 0x07E0
#define C_ORANGE 0xFD20
#define C_RED 0xF800

// Splash palette
#define C_SPLASH_BG 0xFFFF
#define C_SPLASH_TX C_TEAL

// -------------------- PWM (ESP32 Arduino core v3) --------------------
static const int PWM_FREQ = 5000;
static const int PWM_RES = 8; // 0..255
String htmlPage();
void handleRoot();
void handleSave();
void wifiStart();
void wifiStop();
void drawQRFull();

static inline void pwmInit(uint8_t bl, uint8_t led) {
  ledcAttach(TFT_BL, PWM_FREQ, PWM_RES);
  ledcWrite(TFT_BL, bl);

  ledcAttach(LED_PIN, PWM_FREQ, PWM_RES);
  ledcWrite(LED_PIN, led);

  ledcAttach(BUZZER_PIN, 2000, 8);
  ledcWriteTone(BUZZER_PIN, 0);
}
static inline void setBacklight(uint8_t v) { ledcWrite(TFT_BL, v); }
static inline void setLed(uint8_t v) { ledcWrite(LED_PIN, v); }
static inline void buzzerOff() { ledcWriteTone(BUZZER_PIN, 0); }
static inline void buzzerTone(uint16_t hz) { ledcWriteTone(BUZZER_PIN, hz); }

// -------------------- App model --------------------
struct Config {
  uint8_t workMin = 25;
  uint8_t breakMin = 5;
  uint8_t screenBright = 200;
  uint8_t ledBright = 180;
  bool soundOn = true;
} cfg;

struct TimerState {
  bool running = false;
  bool finished = false;
  uint32_t startMs = 0;
  uint32_t pausedMs = 0;
} st;

// Tasks (3)
String tasks[3] = {"Empty", "Empty", "Empty"};
bool taskDone[3] = {false, false, false};
int tasksSel = 0;

// -------------------- STREAK 7x16 (112 days) --------------------
#define STREAK_ROWS 7
#define STREAK_COLS 16
#define STREAK_N (STREAK_ROWS * STREAK_COLS)

uint8_t streak[STREAK_N]; // 0..3
int currentDayIndex = 0;  // "today"
int streakCursor = 0;     // cursor position (row-major)
bool streakEditing = false;

// Clock (settable from millis offset)
int clockH = 12, clockM = 0, clockS = 0;
uint32_t clockSetAtMs = 0;
bool clockEditing = false;
int clockField = 0; // 0=H,1=M,2=S

// SETTINGS
int settingsSel = 0;
bool settingsEditing = false;

// -------------------- Encoder handling (responsive but stable)
// --------------------
int lastClk = HIGH;
uint32_t lastEncMoveAt = 0;
const uint32_t ENC_MIN_INTERVAL_MS = 6; // responsive
const int ENC_STEPS_PER_DETENT = 1;
int encAccum = 0;

bool lastEncBtn = HIGH;
uint32_t encBtnDownAt = 0;
const uint32_t ENC_LONGPRESS_MS = 900;

// Mode button
bool lastModeBtn = HIGH;
uint32_t lastModeAt = 0;
const uint32_t MODE_DEBOUNCE_MS = 250;

// UI cache
bool baseDrawn = false;
int lastShownMin = -1, lastShownSec = -1;

// Alarm
uint32_t lastAnimAt = 0;
uint32_t lastBeepAt = 0;
bool alarmFlip = false;

// -------------------- Helpers --------------------
static inline uint8_t clampU8(int v, int lo, int hi) {
  if (v < lo)
    return (uint8_t)lo;
  if (v > hi)
    return (uint8_t)hi;
  return (uint8_t)v;
}
static inline uint32_t phaseDurationMs() {
  uint8_t m = (phase == PHASE_WORK) ? cfg.workMin : cfg.breakMin;
  return (uint32_t)m * 60UL * 1000UL;
}
static inline uint32_t elapsedMs() {
  if (!st.running)
    return st.pausedMs;
  return st.pausedMs + (millis() - st.startMs);
}
static inline uint32_t remainingMs() {
  uint32_t dur = phaseDurationMs();
  uint32_t el = elapsedMs();
  return (el >= dur) ? 0 : (dur - el);
}
static inline uint16_t accentColor() {
  return (phase == PHASE_WORK) ? C_TEAL : C_ORANGE;
}

static inline void startTimer() {
  st.running = true;
  st.finished = false;
  st.startMs = millis();
}
static inline void pauseTimer() {
  st.running = false;
  st.pausedMs = elapsedMs();
}
static inline void resetTimer() {
  st.running = false;
  st.finished = false;
  st.pausedMs = 0;
  st.startMs = millis();
}
static inline void finishTimer() {
  st.running = false;
  st.finished = true;
  st.pausedMs = phaseDurationMs();
  alarmFlip = false;
  lastAnimAt = 0;
  lastBeepAt = 0;
}
static inline void nextPhase() {
  phase = (phase == PHASE_WORK) ? PHASE_BREAK : PHASE_WORK;
  resetTimer();
  baseDrawn = false;
}

// -------------------- Storage --------------------
void loadConfig() {
  pref.begin("tasky", true);
  cfg.workMin = pref.getUChar("work", 25);
  cfg.breakMin = pref.getUChar("break", 5);
  cfg.screenBright = pref.getUChar("bl", 200);
  cfg.ledBright = pref.getUChar("led", 180);
  cfg.soundOn = pref.getBool("snd", true);

  for (int i = 0; i < 3; i++) {
    String key = "t" + String(i);
    tasks[i] = pref.getString(key.c_str(), "Empty");
    String keyd = "d" + String(i);
    taskDone[i] = pref.getBool(keyd.c_str(), false);
  }

  for (int i = 0; i < STREAK_N; i++) {
    String k = "s" + String(i);
    streak[i] = pref.getUChar(k.c_str(), 0);
  }
  currentDayIndex = (int)pref.getUChar("day", 0);
  if (currentDayIndex < 0 || currentDayIndex >= STREAK_N)
    currentDayIndex = 0;
  streakCursor = currentDayIndex;

  clockH = (int)pref.getUChar("ch", 12);
  clockM = (int)pref.getUChar("cm", 0);
  clockS = (int)pref.getUChar("cs", 0);
  pref.end();

  // limits
  cfg.workMin = clampU8(cfg.workMin, 5, 90); // per request
  cfg.breakMin =
      clampU8(cfg.breakMin, 5, 90); // per request (always 5-min steps; max 90)
  if (cfg.screenBright < 20)
    cfg.screenBright = 20;
  if (clockH < 0 || clockH > 23)
    clockH = 12;
  if (clockM < 0 || clockM > 59)
    clockM = 0;
  if (clockS < 0 || clockS > 59)
    clockS = 0;
}

void saveConfig() {
  pref.begin("tasky", false);
  pref.putUChar("work", cfg.workMin);
  pref.putUChar("break", cfg.breakMin);
  pref.putUChar("bl", cfg.screenBright);
  pref.putUChar("led", cfg.ledBright);
  pref.putBool("snd", cfg.soundOn);

  for (int i = 0; i < 3; i++) {
    String key = "t" + String(i);
    pref.putString(key.c_str(), tasks[i]);
    String keyd = "d" + String(i);
    pref.putBool(keyd.c_str(), taskDone[i]);
  }

  for (int i = 0; i < STREAK_N; i++) {
    String k = "s" + String(i);
    pref.putUChar(k.c_str(), streak[i]);
  }
  pref.putUChar("day", (uint8_t)currentDayIndex);

  pref.putUChar("ch", (uint8_t)clockH);
  pref.putUChar("cm", (uint8_t)clockM);
  pref.putUChar("cs", (uint8_t)clockS);
  pref.end();
}

// -------------------- Text helpers --------------------
void centerTextFont(const char *s, int baselineY, uint16_t color,
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

void rightTextFont(const String &s, int xRight, int baselineY, uint16_t color,
                   const GFXfont *font) {
  tft.setTextColor(color);
  tft.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds((char *)s.c_str(), 0, baselineY, &x1, &y1, &w, &h);
  tft.setCursor(xRight - (int)w, baselineY);
  tft.print(s);
  tft.setFont();
}

// -------------------- Splash --------------------
void drawSplashTasky() {
  tft.fillScreen(C_SPLASH_BG);
  tft.setTextColor(C_SPLASH_TX);
  tft.setFont(&FreeSansBold24pt7b);

  const char *s = "Tasky";
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds((char *)s, 0, 0, &x1, &y1, &w, &h);

  // slightly above center
  int baseline = 128;
  tft.setCursor((W - (int)w) / 2, baseline);
  tft.print(s);

  tft.setFont();
}

// -------------------- TIMER UI --------------------
void drawTimerBase() {
  tft.fillScreen(C_BG);

  // subtle border
  tft.drawRoundRect(10, 10, 220, 220, 16, C_LINE);

  // phase tiny indicator dot + label-less (you wanted no WORK word)
  uint16_t dot = accentColor();
  tft.fillCircle(120, 22, 3, dot);

  baseDrawn = true;
  lastShownMin = -1;
  lastShownSec = -1;
}

void drawTimerTime(int mm, int ss) {
  // clear time area
  tft.fillRect(16, 52, 208, 120, C_BG);

  char buf[8];
  snprintf(buf, sizeof(buf), "%02d:%02d", mm, ss);

  tft.setTextColor(C_TEXT);
  tft.setFont(&FreeSansBold24pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);

  // keep it visually centered and dominant
  int baseline = 120;
  tft.setCursor((W - (int)w) / 2, baseline);
  tft.print(buf);
  tft.setFont();

  // status only when paused/finished
  tft.fillRect(16, 166, 208, 26, C_BG);

  if (st.finished) {
    centerTextFont("DONE", 186, C_RED, &FreeSansBold12pt7b);
  } else if (!st.running) {
    // paused indicator only (no RUNNING)
    centerTextFont("PAUSED", 186, C_MUTED, &FreeSansBold12pt7b);
  }

  // subtle bottom separator
  tft.drawFastHLine(30, 205, 180, C_LINE);

  // small hint-less minimalist footer dot
  tft.fillCircle(120, 216, 2, C_LINE);
}

// -------------------- SETTINGS UI --------------------
void drawSettings() {
  tft.fillScreen(C_BG);
  centerTextFont("SETTINGS", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  const char *labels[5] = {"Work", "Break", "Sound", "Brightness", "LED"};
  String values[5];
  values[0] = String(cfg.workMin) + "m";
  values[1] = String(cfg.breakMin) + "m";
  values[2] = cfg.soundOn ? "ON" : "OFF";
  values[3] = String((cfg.screenBright * 100) / 255) + "%";
  values[4] = String((cfg.ledBright * 100) / 255) + "%";

  int y0 = 74;
  int rowH = 30;

  for (int i = 0; i < 5; i++) {
    int baseline = y0 + i * rowH;

    bool sel = (i == settingsSel);

    // soft dark background highlight (as requested)
    if (sel) {
      uint16_t bg = settingsEditing ? 0x0841 : 0x1082; // darker when editing
      tft.fillRoundRect(14, baseline - 18, 212, 26, 10, bg);
      tft.drawRoundRect(14, baseline - 18, 212, 26, 10,
                        settingsEditing ? C_TEAL : C_LINE);
    }

    tft.setTextColor(C_TEXT);
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(22, baseline);
    tft.print(labels[i]);

    rightTextFont(values[i], 220, baseline, sel ? C_SOFT : C_TEXT,
                  &FreeSans12pt7b);
    tft.setFont();
  }
}

// -------------------- TASKS UI --------------------
void drawTasks() {
  tft.fillScreen(C_BG);
  centerTextFont("TASKS", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  int y0 = 80;
  int rowH = 44;

  for (int i = 0; i < 3; i++) {
    int baseline = y0 + i * rowH;
    bool sel = (i == tasksSel);

    if (sel) {
      // subtle gray highlight + teal stroke (Nothing-ish)
      tft.fillRoundRect(12, baseline - 22, 216, 34, 12, 0x0841);
      tft.drawRoundRect(12, baseline - 22, 216, 34, 12, C_TEAL);
    }

    uint16_t boxC = taskDone[i] ? C_GREEN : C_MUTED;
    tft.drawRoundRect(22, baseline - 10, 18, 18, 4, boxC);
    if (taskDone[i]) {
      tft.drawLine(26, baseline - 2, 30, baseline + 4, C_GREEN);
      tft.drawLine(30, baseline + 4, 38, baseline - 6, C_GREEN);
    }

    tft.setTextColor(taskDone[i] ? C_MUTED : C_TEXT);
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(46, baseline);
    String t = tasks[i];
    if (t.length() > 16)
      t = t.substring(0, 16) + "...";
    tft.print(t);
    tft.setFont();
  }
}

// -------------------- STREAK UI (row-major: top-left -> right)
// --------------------
uint16_t streakColor(uint8_t v) {
  if (v == 0)
    return C_LINE;
  if (v == 1)
    return 0x1B45;
  if (v == 2)
    return 0x2E47;
  return C_GREEN;
}

void drawStreak() {
  tft.fillScreen(C_BG);
  centerTextFont("STREAK", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  int sz = 10;
  int gap = 3;

  int gridW = STREAK_COLS * sz + (STREAK_COLS - 1) * gap;
  int gridH = STREAK_ROWS * sz + (STREAK_ROWS - 1) * gap;

  int gx = (W - gridW) / 2;
  int gy = 70;

  // idx = r*COLS + c  (top-left -> right)
  for (int r = 0; r < STREAK_ROWS; r++) {
    for (int c = 0; c < STREAK_COLS; c++) {
      int idx = r * STREAK_COLS + c;
      int x = gx + c * (sz + gap);
      int y = gy + r * (sz + gap);

      tft.fillRoundRect(x, y, sz, sz, 2, streakColor(streak[idx]));

      if (idx == currentDayIndex)
        tft.drawRoundRect(x - 1, y - 1, sz + 2, sz + 2, 3, C_TEAL);
      if (idx == streakCursor)
        tft.drawRoundRect(x - 2, y - 2, sz + 4, sz + 4, 4, C_TEXT);
    }
  }

  // minimal instruction (small, not bottom clutter)
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_MUTED);
  if (!streakEditing)
    centerTextFont("Click: edit day", 224, C_MUTED, &FreeSans9pt7b);
  else
    centerTextFont("Rotate: 0..3  Click: save", 224, C_MUTED, &FreeSans9pt7b);
  tft.setFont();
}

// -------------------- CLOCK UI --------------------
void getClockNow(int &h, int &m, int &s) {
  uint32_t deltaMs = (millis() - clockSetAtMs);
  uint32_t deltaSec = deltaMs / 1000UL;

  int total = clockH * 3600 + clockM * 60 + clockS + (int)deltaSec;
  total %= (24 * 3600);
  if (total < 0)
    total += 24 * 3600;

  h = total / 3600;
  int rem = total % 3600;
  m = rem / 60;
  s = rem % 60;
}

void drawClock() {
  tft.fillScreen(C_BG);
  centerTextFont("CLOCK", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  int hh, mm, ss;
  getClockNow(hh, mm, ss);

  char buf[10];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hh, mm, ss);

  tft.setTextColor(C_TEXT);
  tft.setFont(&FreeSansBold18pt7b);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  int baseline = 134;
  tft.setCursor((W - (int)w) / 2, baseline);
  tft.print(buf);
  tft.setFont();

  // EVERY SECOND COUNTS (smaller, elegant)
  centerTextFont("EVERY SECOND COUNTS", 168, C_MUTED, &FreeSans9pt7b);

  // tiny edit indicator (only when editing)
  if (clockEditing) {
    tft.fillRoundRect(82, 184, 76, 18, 8, 0x0841);
    tft.drawRoundRect(82, 184, 76, 18, 8, C_TEAL);
    if (clockField == 0)
      centerTextFont("EDIT H", 198, C_SOFT, &FreeSans9pt7b);
    else if (clockField == 1)
      centerTextFont("EDIT M", 198, C_SOFT, &FreeSans9pt7b);
    else
      centerTextFont("EDIT S", 198, C_SOFT, &FreeSans9pt7b);
  }
}

// -------------------- WIFI + Web --------------------

String htmlPage() {
  String s;
  s += "<!doctype html><html><head>";
  s += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<style>body{font-family:Arial;background:#111;color:#fff;padding:16px}";
  s += "input,button{width:100%;padding:12px;margin:8px 0;font-size:16px}";
  s += "button{background:#00e5cc;border:none;color:#000;font-weight:bold}</"
       "style>";
  s += "</head><body>";

  s += "<h2>Tasky Setup</h2>";

  s += "<form method='POST' action='/save'>";
  for (int i = 0; i < 3; i++) {
    s += "<label>Task " + String(i + 1) + "</label>";
    s += "<input name='t" + String(i) + "' value='" + tasks[i] +
         "' maxlength='40'>";
  }

  s += "<label>Pomodoro Minutes</label>";
  s += "<input name='work' value='" + String(cfg.workMin) + "'>";

  s += "<button type='submit'>Save</button>";
  s += "</form>";

  s += "</body></html>";
  return s;
}

void handleRoot() { server.send(200, "text/html", htmlPage()); }

void handleSave() {
  for (int i = 0; i < 3; i++) {
    String k = "t" + String(i);
    if (server.hasArg(k)) {
      tasks[i] = server.arg(k);
      if (tasks[i].length() == 0)
        tasks[i] = "Empty";
      taskDone[i] = false;
    }
  }

  if (server.hasArg("work")) {
    int v = server.arg("work").toInt();
    if (v < 5)
      v = 5;
    if (v > 90)
      v = 90;
    cfg.workMin = (uint8_t)v;
  }

  saveConfig();
  server.send(200, "text/html", "<h3>Saved ✅</h3><p><a href='/'>Back</a></p>");
}

void wifiStart() {
  if (wifiOn)
    return;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str(), apPass.c_str());

  IPAddress ip = WiFi.softAPIP();

  // DNS hijack: todo dominio → Tasky
  dnsServer.start(DNS_PORT, "*", ip);

  // Portal principal
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);

  // Android captive portal
  server.on("/generate_204", HTTP_GET, []() {
    server.sendHeader(
        "Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  server.on("/gen_204", HTTP_GET, []() {
    server.sendHeader(
        "Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  server.on("/connecttest.txt", HTTP_GET, []() { // Windows / algunos Android
    server.sendHeader(
        "Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  server.on("/redirect", HTTP_GET, []() { // algunos Android
    server.sendHeader(
        "Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  server.on("/canonical.html", HTTP_GET, []() { // algunos Android antiguos
    server.sendHeader(
        "Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });

  // iOS / macOS
  server.on("/hotspot-detect.html", HTTP_GET,
            []() { server.send(200, "text/html", htmlPage()); });

  // Windows
  server.on("/ncsi.txt", HTTP_GET,
            []() { server.send(200, "text/plain", "Microsoft NCSI"); });

  // Catch-all
  server.onNotFound([]() {
    server.sendHeader(
        "Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });
  server.on("/ping", HTTP_GET,
            []() { server.send(200, "text/plain", "pong"); });

  server.begin();
  wifiOn = true;
}

void wifiStop() {
  if (!wifiOn)
    return;
  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiOn = false;
}

void drawWifi() {
  tft.fillScreen(C_BG);
  centerTextFont("WIFI", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  tft.setTextColor(C_TEXT);
  tft.setFont(&FreeSans12pt7b);

  tft.setCursor(18, 84);
  tft.print("SSID:");
  tft.setCursor(80, 84);
  tft.print(apSSID);

  tft.setCursor(18, 116);
  tft.print("PASS:");
  tft.setCursor(80, 116);
  tft.print(apPass);

  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(C_MUTED);
  centerTextFont("Click to show QR", 170, C_MUTED, &FreeSans9pt7b);

  String ip = wifiOn ? WiFi.softAPIP().toString() : String("192.168.4.1");
  tft.setTextColor(C_SOFT);
  centerTextFont(ip.c_str(), 204, C_SOFT, &FreeSans9pt7b);
  tft.setFont();
}

// -------------------- QR (WiFi credentials) --------------------
// Text encoded (WiFi QR):
// WIFI:T:WPA;S:Tasky;P:EverySecondCounts;H:false;;

#define QR_SZ 33
static const uint64_t QR_ROWS[QR_SZ] PROGMEM = {
    0x1FC69DC7FULL, 0x1042F8141ULL, 0x175C8EF5DULL, 0x175E7365DULL,
    0x1755A4A5DULL, 0x1057F1D41ULL, 0x1FD55557FULL, 0x001260800ULL,
    0x17C1E967CULL, 0x118212B81ULL, 0x13E237F89ULL, 0x07CFAE0F1ULL,
    0x0B5F7BEFCULL, 0x1C50B28C1ULL, 0x03F6F1F5DULL, 0x09939C4C9ULL,
    0x1AFAF9F3CULL, 0x0FD76F6B4ULL, 0x017A2A944ULL, 0x1D6F50FFCull,
    0x00134C000ULL, 0x1FD11F7FFULL, 0x1042E0C41ULL, 0x175A4BF5DULL,
    0x175FFEF5DULL, 0x17586D25DULL, 0x105FF2441ULL, 0x1FC956BFFULL,
    0x000000000ULL, 0x000000000ULL, 0x000000000ULL, 0x000000000ULL,
    0x000000000ULL};

// -------------------- Alarm --------------------
void alarmStop() {
  buzzerOff();
  setLed(0);
}
void alarmUpdate() {
  uint32_t now = millis();

  if (now - lastAnimAt >= 150) {
    lastAnimAt = now;
    alarmFlip = !alarmFlip;
    setLed(alarmFlip ? cfg.ledBright : 0);
  }

  if (cfg.soundOn) {
    if (now - lastBeepAt >= 260) {
      lastBeepAt = now;
      if (alarmFlip)
        buzzerTone(2200);
      else
        buzzerOff();
    }
  }
}

// -------------------- Streak increment when task done --------------------
void streakAddCompletion() {
  if (streak[currentDayIndex] < 3)
    streak[currentDayIndex]++;
  saveConfig();
}

// -------------------- Encoder read --------------------
int encoderStep() {
  int clk = digitalRead(ENCODER_CLK);

  if (clk != lastClk && clk == LOW) {
    uint32_t now = millis();
    if (now - lastEncMoveAt < ENC_MIN_INTERVAL_MS) {
      lastClk = clk;
      return 0;
    }
    lastEncMoveAt = now;

    int dt = digitalRead(ENCODER_DT);
    // direction (already correct for you)
    int dir = (dt == LOW) ? -1 : +1;

    encAccum += dir;
    lastClk = clk;

    if (abs(encAccum) >= ENC_STEPS_PER_DETENT) {
      int out = (encAccum > 0) ? +1 : -1;
      encAccum = 0;
      return out;
    }
    return 0;
  }

  lastClk = clk;
  return 0;
}

EncClickType encoderClickType() {
  bool b = digitalRead(ENCODER_SW);

  if (b == LOW && lastEncBtn == HIGH) {
    encBtnDownAt = millis();
  }

  if (b == HIGH && lastEncBtn == LOW) {
    uint32_t press = millis() - encBtnDownAt;
    lastEncBtn = b;
    if (press >= ENC_LONGPRESS_MS)
      return ENC_LONG;
    return ENC_SHORT;
  }

  lastEncBtn = b;
  return ENC_NONE;
}

bool modeClick() {
  bool b = digitalRead(MODO_BTN);
  bool clicked = false;
  if (b == LOW && lastModeBtn == HIGH) {
    uint32_t now = millis();
    if (now - lastModeAt > MODE_DEBOUNCE_MS) {
      clicked = true;
      lastModeAt = now;
    }
  }
  lastModeBtn = b;
  return clicked;
}

// -------------------- Navigation --------------------
void goToScreen(Screen s) {
  // stop alarm visuals if leaving timer finished
  if (st.finished && s != SCR_TIMER)
    alarmStop();

  // WiFi ON solo en WIFI_INFO o WIFI_QR
  if (s == SCR_WIFI_INFO || s == SCR_WIFI_QR)
    wifiStart();
  else
    wifiStop();

  screen = s;
  // uiMode = UI_BROWSE;
  baseDrawn = false;

  // reset per-screen edit flags
  settingsEditing = false;
  clockEditing = false;
  streakEditing = false;
}

void drawQRFull() {
  tft.invertDisplay(false); // una vez

  // QR WiFi estándar:
  // WIFI:T:WPA;S:Tasky;P:EverySecondCounts;H:false;;
  String payload = "WIFI:T:WPA;S:" + apSSID + ";P:" + apPass + ";H:false;;";

  QRCode qrcode;

  // Para WiFi a veces no cabe en v1, usamos auto: v3 suele ser seguro
  const uint8_t version = 3; // 29x29
  const uint8_t ecc = ECC_LOW;

  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  qrcode_initText(&qrcode, qrcodeData, version, ecc, payload.c_str());

  tft.fillScreen(QR_WHITE);

  const int QUIET = 4;                   // margen recomendado
  int qrSize = qrcode.size;              // 29
  int modulesTotal = qrSize + QUIET * 2; // 37

  // cell máximo que cabe en 240px:
  int cell = 240 / modulesTotal; // 240/37 = 6
  if (cell < 4)
    cell = 4; // seguridad

  int fullSize = modulesTotal * cell;
  int xFull = (W - fullSize) / 2;
  int yFull = (H - fullSize) / 2;

  // fondo blanco
  tft.fillRect(xFull, yFull, fullSize, fullSize, QR_WHITE);

  int x0 = xFull + QUIET * cell;
  int y0 = yFull + QUIET * cell;

  for (int y = 0; y < qrSize; y++) {
    for (int x = 0; x < qrSize; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        tft.fillRect(x0 + x * cell, y0 + y * cell, cell, cell, QR_BLACK);
      }
    }
  }
}

// -------------------- Setup / Loop --------------------
void setup() {
  Serial.begin(115200);

  // Backlight safe HIGH first
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(MODO_BTN, INPUT_PULLUP);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);

  // ST7789 init (square 240x240)
  tft.init(240, 240);
  tft.setRotation(2); // FIX: upside-down -> rotate 180
  tft.fillScreen(C_BG);

  loadConfig();
  pwmInit(cfg.screenBright, 0);
  setBacklight(cfg.screenBright);
  setLed(0);
  buzzerOff();

  clockSetAtMs = millis();

  drawSplashTasky();
  delay(1300);

  goToScreen(SCR_TIMER);
}

void loop() {
  if (wifiOn) {
    dnsServer.processNextRequest();
    server.handleClient();
  }

  // Mode cycles screens (QR not in cycle; accessed from WIFI click)
  if (modeClick()) {
    // Si estás en QR, volver a WIFI
    if (screen == SCR_WIFI_QR) {
      goToScreen(SCR_WIFI_INFO);
      return;
    }

    // Orden deseado:
    // TIMER -> TASKS -> STREAK -> CLOCK -> WIFI -> SETTINGS -> TIMER ...
    switch (screen) {
    case SCR_TIMER:
      goToScreen(SCR_TASKS);
      break;
    case SCR_TASKS:
      goToScreen(SCR_STREAK);
      break;
    case SCR_STREAK:
      goToScreen(SCR_CLOCK);
      break;
    case SCR_CLOCK:
      goToScreen(SCR_WIFI_INFO);
      break;
    case SCR_WIFI_INFO:
      goToScreen(SCR_SETTINGS);
      break;
    case SCR_SETTINGS:
      goToScreen(SCR_TIMER);
      break;
    default:
      goToScreen(SCR_TIMER);
      break;
    }
  }

  int step = encoderStep();
  EncClickType ctype = encoderClickType();
  bool shortClick = (ctype == ENC_SHORT);

  // -------------------- TIMER --------------------
  if (screen == SCR_TIMER) {
    if (!baseDrawn) {
      drawTimerBase();
      baseDrawn = true;
    }

    // finished -> alarm + click to next phase
    if (st.finished) {
      alarmUpdate();
      if (shortClick) {
        alarmStop();
        nextPhase();
        drawTimerBase();
        baseDrawn = true;
        // draw initial time
        uint32_t rem = remainingMs();
        drawTimerTime((int)(rem / 60000UL), (int)((rem % 60000UL) / 1000UL));
        lastShownMin = -1;
        lastShownSec = -1;
      }
      return;
    }

    // Click toggles start/pause (no long-press needed)
    if (shortClick) {
      if (!st.running) {
        // start from current paused value
        st.running = true;
        st.startMs = millis();
      } else {
        pauseTimer();
      }
    }

    // Rotate ALWAYS adjusts minutes by 5 (per request), limit 90, resets timer
    // to 00
    if (step != 0) {
      int delta = step * 5;

      if (phase == PHASE_WORK) {
        int v = (int)cfg.workMin + delta;
        if (v < 5)
          v = 5;
        if (v > 90)
          v = 90;
        cfg.workMin = (uint8_t)v;
      } else {
        int v = (int)cfg.breakMin + delta;
        if (v < 5)
          v = 5;
        if (v > 90)
          v = 90;
        cfg.breakMin = (uint8_t)v;
      }
      saveConfig();
      resetTimer();
      drawTimerBase();
      baseDrawn = true;
    }

    // finish check
    if (st.running && remainingMs() == 0) {
      finishTimer();
      return;
    }

    // update text only when changed
    static uint32_t lastTextAt = 0;
    uint32_t now = millis();
    if (now - lastTextAt >= 120) {
      lastTextAt = now;
      uint32_t rem = remainingMs();
      int mm = (int)(rem / 60000UL);
      int ss = (int)((rem % 60000UL) / 1000UL);
      if (mm != lastShownMin || ss != lastShownSec) {
        drawTimerTime(mm, ss);
        lastShownMin = mm;
        lastShownSec = ss;
      }
    }

    // outputs off unless alarm
    setLed(0);
    buzzerOff();
    return;
  }

  // -------------------- SETTINGS --------------------
  if (screen == SCR_SETTINGS) {
    if (!baseDrawn) {
      drawSettings();
      baseDrawn = true;
    }

    if (!settingsEditing) {
      if (step != 0) {
        settingsSel += step;
        if (settingsSel < 0)
          settingsSel = 4;
        if (settingsSel > 4)
          settingsSel = 0;
        drawSettings();
      }
      if (shortClick) {
        settingsEditing = true;
        drawSettings();
      }
    } else {
      if (step != 0) {
        if (settingsSel == 0) {
          int v = (int)cfg.workMin + step * 5;
          cfg.workMin = clampU8(v, 5, 90);
        } else if (settingsSel == 1) {
          int v = (int)cfg.breakMin + step * 5;
          cfg.breakMin = clampU8(v, 5, 90);
        } else if (settingsSel == 2) {
          cfg.soundOn = !cfg.soundOn;
        } else if (settingsSel == 3) {
          int v = (int)cfg.screenBright + step * 12;
          cfg.screenBright = clampU8(v, 20, 255);
          setBacklight(cfg.screenBright);
        } else if (settingsSel == 4) {
          int v = (int)cfg.ledBright + step * 12;
          cfg.ledBright = clampU8(v, 0, 255);
        }
        drawSettings();
      }
      if (shortClick) {
        settingsEditing = false;
        saveConfig();
        drawSettings();
      }
    }
    return;
  }

  // -------------------- TASKS --------------------
  if (screen == SCR_TASKS) {
    if (!baseDrawn) {
      drawTasks();
      baseDrawn = true;
    }

    if (step != 0) {
      tasksSel += step;
      if (tasksSel < 0)
        tasksSel = 2;
      if (tasksSel > 2)
        tasksSel = 0;
      drawTasks();
    }

    if (shortClick) {
      taskDone[tasksSel] = !taskDone[tasksSel];
      if (taskDone[tasksSel])
        streakAddCompletion();
      saveConfig();
      drawTasks();
    }
    return;
  }

  // -------------------- STREAK --------------------
  if (screen == SCR_STREAK) {
    if (!baseDrawn) {
      drawStreak();
      baseDrawn = true;
    }

    if (!streakEditing) {
      if (step != 0) {
        // move cursor left->right in row-major
        streakCursor += step;
        if (streakCursor < 0)
          streakCursor = STREAK_N - 1;
        if (streakCursor >= STREAK_N)
          streakCursor = 0;
        drawStreak();
      }
      if (shortClick) {
        // enter edit at cursor
        streakEditing = true;
        currentDayIndex = streakCursor;
        drawStreak();
      }
    } else {
      if (step != 0) {
        int v = (int)streak[currentDayIndex] + step;
        if (v < 0)
          v = 0;
        if (v > 3)
          v = 3;
        streak[currentDayIndex] = (uint8_t)v;
        drawStreak();
      }
      if (shortClick) {
        streakEditing = false;
        saveConfig();
        streakCursor = currentDayIndex;
        drawStreak();
      }
    }
    return;
  }

  // -------------------- CLOCK --------------------
  if (screen == SCR_CLOCK) {
    if (!baseDrawn) {
      drawClock();
      baseDrawn = true;
    }

    if (!clockEditing) {
      if (shortClick) {
        clockEditing = true;
        clockField = 0;
        drawClock();
      }

      static uint32_t lastC = 0;
      uint32_t now = millis();
      if (now - lastC >= 250) {
        lastC = now;
        drawClock();
      }
    } else {
      if (step != 0) {
        if (clockField == 0) {
          clockH += step;
          if (clockH < 0)
            clockH = 23;
          if (clockH > 23)
            clockH = 0;
        } else if (clockField == 1) {
          clockM += step;
          if (clockM < 0)
            clockM = 59;
          if (clockM > 59)
            clockM = 0;
        } else {
          clockS += step;
          if (clockS < 0)
            clockS = 59;
          if (clockS > 59)
            clockS = 0;
        }
        clockSetAtMs = millis();
        drawClock();
      }

      if (shortClick) {
        if (clockField == 0)
          clockField = 1;
        else if (clockField == 1)
          clockField = 2;
        else {
          clockEditing = false;
          saveConfig();
        }
        drawClock();
      }
    }
    return;
  }

  // -------------------- WIFI --------------------
  if (screen == SCR_WIFI_INFO) {
    if (!baseDrawn) {
      drawWifi();
      baseDrawn = true;
    }

    if (shortClick) {
      goToScreen(SCR_WIFI_QR);
    }
    return;
  }

  // -------------------- QR --------------------
  if (screen == SCR_WIFI_QR) {
    if (!baseDrawn) {
      drawQRFull();
      baseDrawn = true;
    }

    if (shortClick) {
      goToScreen(SCR_WIFI_INFO);
    }
    return;
  }
}