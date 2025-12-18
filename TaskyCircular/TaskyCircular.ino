#include "calendar_service.h"
#include "github_service.h"
#include "model.h"
#include "network_manager.h"
#include "ui.h"
#include "wifi_portal.h"
#include <Arduino.h>
#include <SPI.h>

// ==================== PINS ====================
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

// ==================== MODULES ====================
enum EncClickType { ENC_NONE, ENC_SHORT, ENC_LONG };

TaskyModel model;
TaskyUI ui(&model, TFT_CS, TFT_DC, TFT_RST);
TaskyNetManager net(&model);
CalendarService calendar(&model);
GithubService github(&model);
WiFiPortal portal(&model, &net);

// ==================== STATE ====================
Screen screen = SCR_TIMER;
Screen screenBeforeScreensaver = SCR_TIMER;  // Remember screen before screensaver

// Input state
bool lastEncBtn = HIGH;
uint32_t encBtnDownAt = 0;
bool lastModeBtn = HIGH;
const uint32_t ENC_LONGPRESS_MS = 900;

// Screensaver state (activated by long-press MODE button)
bool screensaverActive = false;

// Mode button click types
enum ModeClickType { MODE_NONE, MODE_SHORT, MODE_LONG };

// Timer alarm state
uint32_t lastAnimAt = 0;
uint32_t lastBeepAt = 0;
uint32_t lastUiBeepAt = 0;
bool alarmFlip = false;

// Audio
uint32_t uiBeepOffAt = 0;

// ==================== HARDWARE HELPERS ====================
void setLed(uint8_t v) { ledcWrite(LED_PIN, v); }
void buzzerOff() { ledcWriteTone(BUZZER_PIN, 0); }
void buzzerTone(uint16_t hz) { ledcWriteTone(BUZZER_PIN, hz); }
void setBacklight(uint8_t v) { ledcWrite(TFT_BL, v); }

void alarmStop() {
  buzzerOff();
  setLed(0);
}

// ==================== INPUT FUNCTIONS ====================

// Simple encoder - detect falling edge of CLK
int encoderStep() {
  static int lastClk = HIGH;
  
  int clk = digitalRead(ENCODER_CLK);
  int dt = digitalRead(ENCODER_DT);
  
  if (clk != lastClk && clk == LOW) {
    lastClk = clk;
    return (dt == HIGH) ? +1 : -1;
  }
  lastClk = clk;
  return 0;
}

// Encoder button with debounce
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
    
    if (press < 80 || (now - lastClickAt) < 200)
      return ENC_NONE;
    
    lastClickAt = now;
    return (press >= ENC_LONGPRESS_MS) ? ENC_LONG : ENC_SHORT;
  }
  lastEncBtn = b;
  return ENC_NONE;
}

// Mode button with long-press detection
ModeClickType modeClickType() {
  static uint32_t modeDownAt = 0;
  bool b = digitalRead(MODO_BTN);

  if (b == LOW && lastModeBtn == HIGH) {
    modeDownAt = millis();
  }

  if (b == HIGH && lastModeBtn == LOW) {
    uint32_t press = millis() - modeDownAt;
    lastModeBtn = b;
    if (press < 80) return MODE_NONE;  // Debounce
    return (press >= 1500) ? MODE_LONG : MODE_SHORT;
  }

  lastModeBtn = b;
  return MODE_NONE;
}

// ==================== SCREENSAVER ====================
// Screensaver is now activated by long-press MODE button (1.5s)
// No timeout-based activation - saves memory and CPU cycles

// ==================== NAVIGATION ====================

void navigateNext() {
  Screen next;
  switch (screen) {
    case SCR_TIMER:    next = SCR_TASKS; break;
    case SCR_TASKS:    next = SCR_CALENDAR; break;
    case SCR_CALENDAR: next = SCR_STREAK; break;
    case SCR_STREAK:   next = SCR_STATS; break;
    case SCR_STATS:    next = SCR_SETTINGS; break;
    case SCR_SETTINGS: next = SCR_QR; break;
    case SCR_QR:       next = SCR_WIFI_INFO; break;
    case SCR_WIFI_INFO: next = SCR_TIMER; break;
    default:           next = SCR_TIMER; break;
  }
  
  // Start WiFi only when needed
  if (next == SCR_WIFI_INFO || next == SCR_QR) {
    if (!portal.wifiOn) portal.begin();
  }
  
  // Stop alarm if leaving timer
  if (model.st.finished && next != SCR_TIMER) {
    alarmStop();
  }
  
  screen = next;
  ui.baseDrawn = false;
  ui.settingsEditing = false;
  ui.streakEditing = false;
  ui.applyTheme();
}

// ==================== SCREEN HANDLERS ====================

void handleTimer(int step, EncClickType click) {
  uint32_t now = millis();
  
  // Draw if needed
  if (!ui.baseDrawn) {
    ui.drawTimerBase();
    ui.baseDrawn = true;
  }
  
  // Timer finished - alarm mode
  if (model.st.finished) {
    // Click: next phase
    if (click == ENC_SHORT) {
      ui.uiTick(true);
      alarmStop();
      model.nextPhase();
      ui.drawTimerBase();
    }
    // Long press: reset
    if (click == ENC_LONG) {
      ui.uiTick(true);
      alarmStop();
      model.resetTimer();
      ui.drawTimerBase();
    }
    
    // Alarm animation
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
  
  // Normal timer mode
  // Click: start/pause
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    if (!model.st.running) {
      model.save();
      model.startTimer();
    } else {
      model.pauseTimer();
    }
    ui.lastShownMin = -1;
  }
  
  // Encoder: change time (only when stopped)
  if (step != 0 && !model.st.running) {
    int delta = step * 5;
    if (model.phase == PHASE_WORK) {
      int v = model.cfg.workMin + delta;
      model.cfg.workMin = constrain(v, 5, 90);
    } else {
      int v = model.cfg.breakMin + delta;
      model.cfg.breakMin = constrain(v, 1, 30);
    }
    model.resetTimer();
    ui.drawTimerBase();
  }
  
  // Long press: reset
  if (click == ENC_LONG) {
    ui.uiTick(true);
    model.resetTimer();
    ui.drawTimerBase();
  }
  
  // Check if timer finished
  if (model.st.running && model.remainingMs() == 0) {
    model.finishTimer();
    ui.drawFinishScreen("WELL DONE");
    ui.uiTick(true);
  }
  
  // Update time display
  static uint32_t lastTextAt = 0;
  if (now - lastTextAt >= 100) {
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
}

void handleTasks(int step, EncClickType click) {
  static bool showingDetail = false;
  
  // Draw if needed
  if (!ui.baseDrawn) {
    ui.drawTasks();
    ui.baseDrawn = true;
    showingDetail = false;
  }
  
  // Detail view mode
  if (showingDetail) {
    if (click == ENC_SHORT || click == ENC_LONG) {
      showingDetail = false;
      ui.drawTasks();
    }
    return;
  }
  
  // Encoder: select task
  if (step != 0) {
    ui.tasksSel += step;
    if (ui.tasksSel < 0) ui.tasksSel = 2;
    if (ui.tasksSel > 2) ui.tasksSel = 0;
    ui.drawTasks();
  }
  
  // Click: toggle done
  if (click == ENC_SHORT) {
    model.taskDone[ui.tasksSel] = !model.taskDone[ui.tasksSel];
    ui.uiTick(true);
    if (model.taskDone[ui.tasksSel]) {
      model.streakAddCompletion();
    }
    model.save();
    ui.drawTasks();
  }
  
  // Long press: show detail
  if (click == ENC_LONG) {
    showingDetail = true;
    ui.uiTick(true);
    ui.drawTaskDetail(ui.tasksSel);
  }
}

void handleCalendar(int step, EncClickType click) {
  // Draw if needed
  if (!ui.baseDrawn) {
    ui.drawCalendar();
    ui.baseDrawn = true;
  }

  // Get days in current month for bounds
  time_t nowTime = time(nullptr);
  int daysInMonth = 31;  // Default
  if (nowTime > 1577836800) {
    struct tm now;
    localtime_r(&nowTime, &now);
    // Simple days in month calculation
    int month = now.tm_mon;
    int year = now.tm_year + 1900;
    const int dpm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    daysInMonth = dpm[month];
    if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
      daysInMonth = 29;
    }
  }

  // Encoder: navigate days
  if (step != 0) {
    ui.calendarSelDay += step;
    if (ui.calendarSelDay < 1) ui.calendarSelDay = daysInMonth;
    if (ui.calendarSelDay > daysInMonth) ui.calendarSelDay = 1;
    ui.drawCalendar();
  }

  // Click: could show day details in the future
  if (click == ENC_SHORT) {
    ui.uiTick(false);
    // For now, just visual feedback - could expand to show events
  }
}

void handleStreak(int step, EncClickType click) {
  // Draw if needed
  if (!ui.baseDrawn) {
    ui.drawStreak();
    ui.baseDrawn = true;
  }
  
  // Encoder moves cursor by days (simple navigation)
  if (step != 0) {
    model.streakCursor += step;
    model.streakCursor = constrain(model.streakCursor, 0, STREAK_N - 1);
    ui.uiTick(false);
    ui.drawStreak();
  }
  
  // Click: toggle value directly (0 -> 1 -> 2 -> 3 -> 0)
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    uint8_t v = model.streak[model.streakCursor];
    v = (v + 1) % 4;
    model.streak[model.streakCursor] = v;
    model.save();
    ui.drawStreak();
  }
}

void handleStats(int step, EncClickType click) {
  // Draw if needed
  if (!ui.baseDrawn) {
    ui.drawStats();
    ui.baseDrawn = true;
  }
  // Stats is view-only
}

void handleSettings(int step, EncClickType click) {
  // Draw if needed
  if (!ui.baseDrawn) {
    ui.drawSettings();
    ui.baseDrawn = true;
  }
  
  if (ui.settingsEditing) {
    // Editing mode - encoder changes value
    if (step != 0) {
      switch (ui.settingsSel) {
        case 0: // Work time
          model.cfg.workMin = constrain(model.cfg.workMin + step * 5, 5, 90);
          break;
        case 1: // Break time
          model.cfg.breakMin = constrain(model.cfg.breakMin + step * 5, 1, 30);
          break;
        case 2: // Sound
          model.cfg.soundOn = !model.cfg.soundOn;
          break;
        case 3: // Brightness
          model.cfg.screenBright = constrain(model.cfg.screenBright + step * 25, 20, 255);
          setBacklight(model.cfg.screenBright);
          break;
        case 4: // LED
          model.cfg.ledBright = constrain(model.cfg.ledBright + step * 25, 0, 255);
          break;
        case 5: // Theme
          model.cfg.darkMode = !model.cfg.darkMode;
          ui.applyTheme();
          break;
      }
      ui.drawSettings();
    }
    
    // Click: save and exit edit mode
    if (click == ENC_SHORT) {
      model.save();
      ui.settingsEditing = false;
      ui.uiTick(true);
      ui.drawSettings();
    }
  } else {
    // Navigation mode - encoder moves selector
    if (step != 0) {
      ui.settingsSel += step;
      if (ui.settingsSel < 0) ui.settingsSel = 5;
      if (ui.settingsSel > 5) ui.settingsSel = 0;
      ui.drawSettings();
    }
    
    // Click: enter edit mode
    if (click == ENC_SHORT) {
      ui.uiTick(true);
      ui.settingsEditing = true;
      ui.drawSettings();
    }
  }
}

void handleQR(int step, EncClickType click) {
  // Draw if needed
  if (!ui.baseDrawn) {
    if (!portal.wifiOn) portal.begin();
    ui.drawQRFull(portal.apSSID, portal.apPass);
    ui.baseDrawn = true;
  }
  
  // Click: go to Timer
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    screen = SCR_TIMER;
    ui.baseDrawn = false;
  }
}

void handleWifi(int step, EncClickType click) {
  // Draw if needed
  if (!ui.baseDrawn) {
    if (!portal.wifiOn) portal.begin();
    
    String ip = net.isConnected() ? net.getIP() : WiFi.softAPIP().toString();
    if (ip == "0.0.0.0") ip = WiFi.softAPIP().toString();
    
    String status = "Status: " + net.getStatusString();
    ui.drawWifi(portal.wifiOn, status, portal.apPass, ip);
    ui.baseDrawn = true;
  }
  
  // Refresh if status changed
  static String lastStatus = "";
  if (net.getStatusString() != lastStatus) {
    lastStatus = net.getStatusString();
    ui.baseDrawn = false;
  }
  
  // Click: go to QR
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    screen = SCR_QR;
    ui.baseDrawn = false;
  }
}

// ==================== SETUP ====================
void setup() {
  delay(500);
  Serial.begin(115200);

  // Display backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Input pins
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(MODO_BTN, INPUT_PULLUP);

  // PWM for LED and buzzer
  ledcAttach(LED_PIN, 5000, 8);
  ledcAttach(BUZZER_PIN, 2000, 8);

  // SPI for display
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);

  // Initialize model and UI
  model.begin();
  ui.init();
  
  digitalWrite(TFT_BL, HIGH);
  setLed(0);
  buzzerOff();

  model.clockSetAtMs = millis();

  // Splash screen
  ui.drawSplashTasky();
  delay(1300);

  // Start network services
  net.begin();
  calendar.begin();
  github.begin();

  // Start on Timer screen
  screen = SCR_TIMER;
  ui.baseDrawn = false;
  ui.applyTheme();
}

// ==================== MAIN LOOP ====================
void loop() {
  uint32_t now = millis();

  // 1. WiFi portal - always process (fast, handles web requests)
  portal.loop();

  // 2. Network manager - check connection status (fast, non-blocking now)
  net.loop();

  // 3. Background services - throttled to prevent blocking
  // Only run these every 5 seconds to keep UI responsive
  static uint32_t lastServiceCheck = 0;
  if (now - lastServiceCheck > 5000) {
    lastServiceCheck = now;
    calendar.loop();  // Will only actually update every 15 min internally
    github.loop();    // Will only actually update every 30 min internally
    model.checkDayChange();  // Non-blocking now
  }

  // 2. Audio tick
  if (uiBeepOffAt && millis() >= uiBeepOffAt) {
    buzzerOff();
    uiBeepOffAt = 0;
  }

  // 3. Read inputs ONCE
  ModeClickType modeClick = modeClickType();
  int step = encoderStep();
  EncClickType click = encoderClickType();

  // 3.5. Long-press MODE = enter screensaver
  if (modeClick == MODE_LONG && !screensaverActive) {
    // Enter screensaver (only if timer not running)
    if (!model.st.running && !model.st.finished) {
      screenBeforeScreensaver = screen;
      screensaverActive = true;
      ui.quoteIndex = random(30);
      ui.drawQuotes();
    }
    return;
  }

  // 3.6. If screensaver active
  if (screensaverActive) {
    // Encoder changes quotes
    if (step != 0) {
      ui.quoteIndex = (ui.quoteIndex + step + 30) % 30;
      ui.drawQuotes();
    }
    // Any button press exits screensaver (MODE short/long OR encoder click)
    if (modeClick != MODE_NONE || click != ENC_NONE) {
      screensaverActive = false;
      screen = screenBeforeScreensaver;
      ui.goToScreen(screen);
    }
    return;
  }

  // 4. Short-press MODE = navigate to next screen
  if (modeClick == MODE_SHORT) {
    navigateNext();
    return;
  }

  // 5. Handle current screen
  switch (screen) {
    case SCR_TIMER:    handleTimer(step, click); break;
    case SCR_TASKS:    handleTasks(step, click); break;
    case SCR_CALENDAR: handleCalendar(step, click); break;
    case SCR_STREAK:   handleStreak(step, click); break;
    case SCR_STATS:    handleStats(step, click); break;
    case SCR_SETTINGS: handleSettings(step, click); break;
    case SCR_QR:       handleQR(step, click); break;
    case SCR_WIFI_INFO: handleWifi(step, click); break;
  }
}
