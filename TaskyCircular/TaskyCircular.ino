#include "calendar_service.h"
#include "github_service.h" // [NEW]
#include "model.h"
#include "network_manager.h"
#include "ui.h"
#include "wifi_portal.h"
#include <Arduino.h>
#include <SPI.h>

// -------------------- PINS --------------------
#define TFT_BL 2
#define TFT_CS 3
#define TFT_DC 4
#define TFT_RST 5
#define TFT_MOSI 10
#define TFT_SCK 8

#define ENCODER_CLK 6
#define ENCODER_DT 7
#define ENCODER_SW 21
#define MODO_BTN 20

#define LED_PIN 9
#define BUZZER_PIN 1

// -------------------- MODULES --------------------
enum EncClickType { ENC_NONE, ENC_SHORT, ENC_LONG };
TaskyModel model;
TaskyUI ui(&model, TFT_CS, TFT_DC, TFT_RST);
TaskyNetManager net(&model);
CalendarService calendar(&model); // [NEW]
GithubService github(&model);     // [NEW]
WiFiPortal portal(&model, &net); // [UPDATED] Order of init matters? net is used
                                 // in portal, so net should exist.
// C++ inits globals in order of definition usually. net defined before portal
// now.

// -------------------- LOGIC STATE --------------------
Screen screen = SCR_TIMER;
bool focusLock = true;
bool finishRitualActive = false;
uint8_t finishRitualStep = 0;
uint32_t finishRitualAt = 0;

// Encoder State
uint32_t lastEncMoveAt = 0;
bool lastEncBtn = HIGH;
uint32_t encBtnDownAt = 0;
const uint32_t ENC_LONGPRESS_MS = 900;
uint32_t lastEncStepAt = 0;

// Buttons
bool lastModeBtn = HIGH;
uint32_t lastModeAt = 0;

// Alarm
uint32_t lastAnimAt = 0;
uint32_t lastBeepAt = 0;
bool alarmFlip = false;

// Audio/LED globals for UI
uint32_t lastUiBeepAt = 0;
uint32_t uiBeepOffAt = 0;

// -------------------- HARDWARE HELPERS --------------------
void setLed(uint8_t v) { ledcWrite(LED_PIN, v); }
void buzzerOff() { ledcWriteTone(BUZZER_PIN, 0); }
void buzzerTone(uint16_t hz) { ledcWriteTone(BUZZER_PIN, hz); }
void setBacklight(uint8_t v) { ledcWrite(TFT_BL, v); }

void uiHaptic() {
  // Only sound, no LED
  uint32_t now = millis();
  if (model.cfg.soundOn && (now - lastUiBeepAt > 60)) {
    lastUiBeepAt = now;
    buzzerTone(2200);
    uiBeepOffAt = now + 10;
  }
}

void alarmStop() {
  buzzerOff();
  setLed(0);
}

void enterScreen(Screen s) {
  if (model.st.finished && s != SCR_TIMER)
    alarmStop();

  // Only start WiFi when needed, don't stop it during navigation
  // This prevents SPI conflicts that cause display issues
  if (s == SCR_WIFI_INFO || s == SCR_QR) {
    if (!portal.wifiOn)
      portal.begin();
  }
  // Note: We don't call portal.stop() here anymore to avoid display issues

  screen = s;
  ui.baseDrawn = false;  // Force redraw on screen change
  
  // Reset editing states
  ui.settingsEditing = false;
  ui.streakEditing = false;

  // Theme & Redraw
  ui.applyTheme();
  ui.goToScreen(s);
}

// -------------------- INPUT --------------------

EncClickType encoderClickType() {
  static uint32_t lastClickAt = 0;
  bool b = digitalRead(ENCODER_SW);
  
  if (b == LOW && lastEncBtn == HIGH) {
    encBtnDownAt = millis();
  }
  if (b == HIGH && lastEncBtn == LOW) {
    uint32_t now = millis();
    uint32_t press = now - encBtnDownAt;
    lastEncBtn = b;
    
    // Ignore if too short (noise) or too soon after last click
    if (press < 150 || (now - lastClickAt) < 300)
      return ENC_NONE;
    
    lastClickAt = now;
    return (press >= ENC_LONGPRESS_MS) ? ENC_LONG : ENC_SHORT;
  }
  lastEncBtn = b;
  return ENC_NONE;
}

int encoderStep() {
  static uint8_t lastAB = 3;
  static int8_t count = 0;
  
  uint8_t a = digitalRead(ENCODER_CLK);
  uint8_t b = digitalRead(ENCODER_DT);
  uint8_t ab = (a << 1) | b;
  
  if (ab == lastAB) return 0;
  
  // Gray code transitions - clockwise
  if ((lastAB == 3 && ab == 1) || (lastAB == 1 && ab == 0) ||
      (lastAB == 0 && ab == 2) || (lastAB == 2 && ab == 3)) {
    count++;
  }
  // Gray code transitions - counter-clockwise  
  else if ((lastAB == 3 && ab == 2) || (lastAB == 2 && ab == 0) ||
           (lastAB == 0 && ab == 1) || (lastAB == 1 && ab == 3)) {
    count--;
  }
  
  lastAB = ab;
  
  // 4 transitions = 1 physical detent
  if (count >= 4) { count = 0; return +1; }
  if (count <= -4) { count = 0; return -1; }
  return 0;
}

bool modeClick() {
  bool b = digitalRead(MODO_BTN);
  bool clicked = false;
  if (b == LOW && lastModeBtn == HIGH) {
    if (millis() - lastModeAt > 250) {
      clicked = true;
      lastModeAt = millis();
    }
  }
  lastModeBtn = b;
  return clicked;
}

// -------------------- SETUP --------------------
void setup() {
  delay(500); // Wait for power stabilization
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(MODO_BTN, INPUT_PULLUP);

  // PWM Init
  // ledcAttach(TFT_BL, 5000, 8); // DEBUG: Disabled PWM
  ledcAttach(LED_PIN, 5000, 8);
  ledcAttach(BUZZER_PIN, 2000, 8);

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);

  model.begin();
  ui.init();

  // setBacklight(model.cfg.screenBright);
  digitalWrite(TFT_BL, HIGH); // Force ON
  setLed(0);
  buzzerOff();

  model.clockSetAtMs = millis();

  ui.drawSplashTasky();
  delay(1300);

  // Start Network
  net.begin();
  calendar.begin(); // [NEW]
  github.begin();   // [NEW]

  // Timer is the main screen - Tasky is a Pomodoro first
  enterScreen(SCR_TIMER);
}

// -------------------- LOOP --------------------
void loop() {
  portal.loop();   // Handle web server
  net.loop();      // Handle wifi connection
  calendar.loop(); // [NEW]
  github.loop();   // [NEW]

  // Audio Tick
  uint32_t now = millis();
  if (uiBeepOffAt && now >= uiBeepOffAt) {
    buzzerOff();
  }

  // --- Mode Button ---
  // Navigation cycle: Timer -> Tasks -> Calendar -> Streak -> Stats -> Settings -> QR -> Timer
  if (modeClick()) {
    if (screen == SCR_TIMER)
      enterScreen(SCR_TASKS);
    else if (screen == SCR_TASKS)
      enterScreen(SCR_CALENDAR);
    else if (screen == SCR_CALENDAR)
      enterScreen(SCR_STREAK);
    else if (screen == SCR_STREAK)
      enterScreen(SCR_STATS);
    else if (screen == SCR_STATS)
      enterScreen(SCR_SETTINGS);
    else if (screen == SCR_SETTINGS)
      enterScreen(SCR_QR);
    else if (screen == SCR_QR)
      enterScreen(SCR_WIFI_INFO);
    else if (screen == SCR_WIFI_INFO)
      enterScreen(SCR_TIMER);
    else
      enterScreen(SCR_TIMER);
    return;
  }
  
  // Ensure Timer is drawn before focusLock can block
  if (screen == SCR_TIMER && !ui.baseDrawn) {
    ui.drawTimerBase();
    ui.baseDrawn = true;
  }
  
  if (focusLock && screen == SCR_TIMER && model.st.running) {
    // Timer is running - only allow viewing, no navigation
    // But still update the display
    uint32_t rem = model.remainingMs();
    int mm = rem / 60000;
    int ss = (rem % 60000) / 1000;
    if (mm != ui.lastShownMin || ss != ui.lastShownSec) {
      ui.drawTimerTime(mm, ss);
      ui.lastShownMin = mm;
      ui.lastShownSec = ss;
    }
    return;
  }

  // --- Encoder ---
  int step = encoderStep();
  EncClickType ctype = encoderClickType();
  bool shortClick = (ctype == ENC_SHORT);

  // --- Screens ---

if (screen == SCR_QR) {
  if (!ui.baseDrawn) {
    // Ensure AP is on so QR is valid? Or just show static info?
    // User wants to scan to create tasks.
    // We'll rely on global portal.begin() if needed, or just assume it's up.
    // Ideally, ensure WiFi is ON here.
    if (!portal.wifiOn)
      portal.begin();

    ui.drawQRFull(portal.apSSID, portal.apPass);
    ui.baseDrawn = true;
  }
  if (shortClick) {
    ui.uiTick(true);
    // Next screen in cycle is TASKS manually or let loop handle mode button?
    // User said "first screen... then timer". Navigation button handles
    // cycle. But maybe click also advances? "crea tasks con el qr... para que
    // sepa que tiene que escanear" Let's allow click to go to TASKS for
    // convenience
    enterScreen(SCR_TIMER); // Actually Loop order says TIMER is next... wait.
    // Switch says QR -> TASKS.
    // Let's stick to MODE button for main nav, but click here can act as
    // "Done"
    enterScreen(SCR_TIMER); // Flow: QR -> Timer -> Tasks
  }
  return;
}

if (screen == SCR_WIFI_INFO) {
  if (!ui.baseDrawn) {
    if (!portal.wifiOn)
      portal.begin();

    // Show real IP if connected, or AP IP
    String ip = net.isConnected() ? net.getIP() : WiFi.softAPIP().toString();
    if (ip == "0.0.0.0")
      ip = WiFi.softAPIP().toString();

    String status = net.getStatusString();
    // We might want to pass status to UI, for now just append to SSID?
    // Or modify drawWifi interface.
    // For now, let's just pass status as "ssid" supplement or update drawWifi
    // later. Hack: append status to SSID string for display?
    String ssidDisplay = "Status: " + status;

    ui.drawWifi(portal.wifiOn, ssidDisplay, portal.apPass, ip);
    ui.baseDrawn = true;
  }

  // Refresh periodically if status changes?
  static String lastStatus = "";
  if (net.getStatusString() != lastStatus) {
    lastStatus = net.getStatusString();
    ui.baseDrawn = false; // Trigger redraw
  }

  if (shortClick) {
    ui.uiTick(true);
    enterScreen(SCR_QR); // Back to QR
  }
  return;
}

if (screen == SCR_CALENDAR) {
  if (!ui.baseDrawn) {
    ui.drawCalendar();
    ui.baseDrawn = true;
  }
  // Refresh periodically if we had logic
  if (shortClick) {
    ui.uiTick(true);
    // Force refresh? Or just wait.
    // Maybe go to next screen or just refresh
    // Let's make it refresh just in case
    // calendar.update(); // TODO
  }
  return;
}

if (screen == SCR_STREAK) {
  if (!ui.baseDrawn) {
    ui.drawStreak();
    ui.baseDrawn = true;
  }

  if (ui.streakEditing) {
    // In edit mode - encoder moves cursor
    if (step != 0) {
      model.streakCursor += step;
      if (model.streakCursor < 0)
        model.streakCursor = 0;
      if (model.streakCursor >= STREAK_N)
        model.streakCursor = STREAK_N - 1;
      ui.uiTick(false);
      ui.drawStreak();
    }

    // Click changes value and exits edit mode
    if (shortClick) {
      ui.uiTick(true);
      uint8_t v = model.streak[model.streakCursor];
      v++;
      if (v > 3)
        v = 0;
      model.streak[model.streakCursor] = v;
      ui.anim.triggerInk(model.streakCursor);
      model.save();
      ui.streakEditing = false;
      ui.drawStreak();
    }
  } else {
    // Not editing - click enters edit mode
    if (shortClick) {
      ui.uiTick(true);
      ui.streakEditing = true;
      ui.drawStreak();
    }
  }
  return;
}

if (screen == SCR_STATS) {
  if (!ui.baseDrawn) {
    ui.drawStats();
    ui.baseDrawn = true;
  }
  // Stats are static - only update when entering screen or completing pomodoro
  // No constant refresh needed
  return;
}

if (screen == SCR_SETTINGS) {
  if (!ui.baseDrawn) {
    ui.drawSettings();
    ui.baseDrawn = true;
  }

  if (!ui.settingsEditing) {
    // Navegación - encoder mueve selector
    if (step != 0) {
      ui.settingsSel += step;
      if (ui.settingsSel < 0) ui.settingsSel = 5;
      if (ui.settingsSel > 5) ui.settingsSel = 0;
      ui.drawSettings();
    }
    // Click entra en modo edición
    if (shortClick) {
      ui.uiTick(true);
      ui.settingsEditing = true;
      ui.drawSettings();
    }
  } else {
    // Editando - encoder cambia valor
    if (step != 0) {
      uiHaptic();
      if (ui.settingsSel == 0)
        model.cfg.workMin = ui.model->clampU8(model.cfg.workMin + step * 5, 5, 90);
      else if (ui.settingsSel == 1)
        model.cfg.breakMin = ui.model->clampU8(model.cfg.breakMin + step * 5, 1, 30);
      else if (ui.settingsSel == 2)
        model.cfg.soundOn = !model.cfg.soundOn;
      else if (ui.settingsSel == 3) {
        model.cfg.screenBright = ui.model->clampU8(model.cfg.screenBright + step * 25, 20, 255);
        setBacklight(model.cfg.screenBright);
      }
      else if (ui.settingsSel == 4)
        model.cfg.ledBright = ui.model->clampU8(model.cfg.ledBright + step * 25, 0, 255);
      else if (ui.settingsSel == 5) {
        model.cfg.darkMode = !model.cfg.darkMode;
        ui.applyTheme();
      }
      ui.drawSettings();
    }
    // Click guarda y sale
    if (shortClick) {
      ui.settingsEditing = false;
      model.save();
      ui.uiTick(true);
      ui.drawSettings();
    }
  }
  return;
}

if (screen == SCR_TASKS) {
  if (!ui.baseDrawn) {
    ui.drawTasks();
    ui.baseDrawn = true;
  }

  static bool showingDetail = false;
  if (showingDetail) {
    if (shortClick || ctype == ENC_LONG) {
      showingDetail = false;
      ui.drawTasks(); // Redraw main list
    }
    return;
  }

  if (step != 0) {
    ui.tasksSel += step;
    if (ui.tasksSel < 0)
      ui.tasksSel = 2;
    if (ui.tasksSel > 2)
      ui.tasksSel = 0;
    ui.drawTasks();
  }

  if (shortClick) {
    model.taskDone[ui.tasksSel] = !model.taskDone[ui.tasksSel];
    ui.uiTick(true);
    if (model.taskDone[ui.tasksSel])
      model.streakAddCompletion();
    model.save();
    ui.drawTasks();
  }

  if (ctype == ENC_LONG) {
    showingDetail = true;
    ui.uiTick(true);
    ui.drawTaskDetail(ui.tasksSel);
  }
  return;
}

if (screen == SCR_TIMER) {
  if (!ui.baseDrawn) {
    ui.drawTimerBase();
    ui.baseDrawn = true;
  }

  // Finished logic
  if (model.st.finished) {
    if (shortClick) {
      ui.uiTick(true);
      alarmStop();
      model.nextPhase();
      ui.drawTimerBase();
    }
    if (ctype == ENC_LONG) {
      ui.uiTick(true);
      alarmStop();
      model.resetTimer();
      ui.drawTimerBase();
    }

    // Alarm blink
    if (now - lastAnimAt >= 150) {
      lastAnimAt = now;
      alarmFlip = !alarmFlip;
      setLed(alarmFlip ? model.cfg.ledBright : 0);
    }
    if (model.cfg.soundOn && now - lastBeepAt >= 260) {
      lastBeepAt = now;
      if (alarmFlip) buzzerTone(2200);
      else buzzerOff();
    }
    ui.drawFlashIfNeeded();
    return;
  }

  // SIMPLE TIMER LOGIC:
  // - Click: start/pause
  // - Encoder: change time by 5 min (only when NOT running)
  // - Long press: reset

  if (shortClick) {
    ui.uiTick(true);
    if (!model.st.running) {
      model.save();
      model.startTimer();
    } else {
      model.pauseTimer();
    }
    ui.lastShownMin = -1;
  }
  
  // Encoder changes time only when timer is stopped
  if (step != 0 && !model.st.running) {
    int delta = step * 5;
    if (model.phase == PHASE_WORK) {
      int v = model.cfg.workMin + delta;
      if (v < 5) v = 5;
      if (v > 90) v = 90;
      model.cfg.workMin = v;
    } else {
      int v = model.cfg.breakMin + delta;
      if (v < 1) v = 1;
      if (v > 30) v = 30;
      model.cfg.breakMin = v;
    }
    model.resetTimer();
    ui.baseDrawn = false;  // Force redraw on next loop iteration
  }
  
  // Long press resets
  if (ctype == ENC_LONG) {
    ui.uiTick(true);
    model.resetTimer();
    ui.drawTimerBase();
  }

  // Check if finished
  if (model.st.running && model.remainingMs() == 0) {
    model.finishTimer();
    ui.drawFinishScreen("WELL DONE");
    ui.uiTick(true);
  }

  // Update display
  static uint32_t lastTextAt = 0;
  if (now - lastTextAt >= 120) {
    lastTextAt = now;
    uint32_t rem = model.remainingMs();
    int mm = rem / 60000;
    int ss = (rem % 60000) / 1000;
    if (mm != ui.lastShownMin || ss != ui.lastShownSec) {
      ui.drawTimerTime(mm, ss);
      ui.lastShownMin = mm;
      ui.lastShownSec = ss;
    }
  }
  return;
}

// Safety: ensure Timer always draws if it's the current screen
if (screen == SCR_TIMER && !ui.baseDrawn) {
  ui.drawTimerBase();
  ui.baseDrawn = true;
}
}