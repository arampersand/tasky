#include "model.h"
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
WiFiPortal portal(&model);

// -------------------- LOGIC STATE --------------------
Screen screen = SCR_TIMER;
bool focusLock = true;
bool finishRitualActive = false;
uint8_t finishRitualStep = 0;
uint32_t finishRitualAt = 0;

// Encoder State
int lastClk = HIGH;
uint32_t lastEncMoveAt = 0;
const uint32_t ENC_MIN_INTERVAL_MS = 6;
const int ENC_STEPS_PER_DETENT = 1;
int encAccum = 0;
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

  if (s == SCR_WIFI_INFO || s == SCR_WIFI_QR) {
    if (!portal.wifiOn)
      portal.begin();
  } else {
    // Only stop wifi if we really want to save power or avoid interference
    // The original code stopped it eagerly.
    if (portal.wifiOn)
      portal.stop();
  }

  screen = s;

  // Theme & Redraw
  ui.applyTheme();
  ui.goToScreen(s);
}

// -------------------- INPUT --------------------

EncClickType encoderClickType() {
  bool b = digitalRead(ENCODER_SW);
  if (b == LOW && lastEncBtn == HIGH) {
    encBtnDownAt = millis();
  }
  if (b == HIGH && lastEncBtn == LOW) {
    uint32_t press = millis() - encBtnDownAt;
    lastEncBtn = b;
    if (press < 50)
      return ENC_NONE; // Debounce: ignore noise < 50ms
    return (press >= ENC_LONGPRESS_MS) ? ENC_LONG : ENC_SHORT;
  }
  lastEncBtn = b;
  return ENC_NONE;
}

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
  ui.init(); // This now handles rotation & basic fill

  // setBacklight(model.cfg.screenBright);
  digitalWrite(TFT_BL, HIGH); // Force ON
  setLed(0);
  buzzerOff();

  model.clockSetAtMs = millis();

  ui.drawSplashTasky();
  delay(1300);

  enterScreen(SCR_TIMER);
}

// -------------------- LOOP --------------------
void loop() {
  portal.loop(); // Handle web server

  // Audio Tick
  uint32_t now = millis();
  if (uiBeepOffAt && now >= uiBeepOffAt) {
    buzzerOff();
    uiBeepOffAt = 0;
  }

  // --- Mode Button ---
  if (modeClick()) {
    if (screen == SCR_WIFI_QR) {
      enterScreen(SCR_WIFI_INFO);
      return;
    }

    if (focusLock && screen == SCR_TIMER && model.st.running) {
      ui.uiTick(true); // Sound + Flash is okay for warning
      // Quick overlay or just ignore logic?
      // Original drew "FOCUS LOCK"
      // We can rely on UI to draw toast, but for now just beep
      return;
    }

    // Cycle
    Screen next = SCR_TIMER;
    switch (screen) {
    case SCR_TIMER:
      next = SCR_TASKS;
      break;
    case SCR_TASKS:
      next = SCR_STREAK;
      break;
    case SCR_STREAK:
      next = SCR_CLOCK;
      break;
    case SCR_CLOCK:
      next = SCR_WIFI_INFO;
      break;
    case SCR_WIFI_INFO:
      next = SCR_SETTINGS;
      break;
    case SCR_SETTINGS:
      next = SCR_TIMER;
      break;
    default:
      next = SCR_TIMER;
      break;
    }
    enterScreen(next);
  }

  // --- Encoder ---
  int step = encoderStep();
  EncClickType ctype = encoderClickType();
  bool shortClick = (ctype == ENC_SHORT);

  // --- Screens ---

  if (screen == SCR_TIMER) {
    if (!ui.baseDrawn)
      ui.drawTimerBase();

    if (model.st.finished) {
      // Logic for ritual
      if (finishRitualActive) {
        if (finishRitualStep == 0) {
          ui.drawFinishScreen("WELL DONE"); // New screen!
          finishRitualStep = 1;
          finishRitualAt = millis(); // use millis(), 'now' usually var
          ui.uiTick(true);
        } else if (finishRitualStep == 1 && millis() - finishRitualAt > 2000) {
          // just wait longer
          finishRitualStep = 2;
        } else if (finishRitualStep == 2) {
          // End of ritual wait
          // Ideally we wait for click?
          // The code below handles click to exit.
        }

        if (shortClick) {
          finishRitualActive = false;
          alarmStop();
          model.nextPhase();
          ui.drawTimerBase();
        }
        ui.drawFlashIfNeeded();
        setLed(0);
        return;
      }

      // Alarm
      if (now - lastAnimAt >= 150) {
        lastAnimAt = now;
        alarmFlip = !alarmFlip;
        setLed(alarmFlip ? model.cfg.ledBright : 0);
      }
      if (model.cfg.soundOn && now - lastBeepAt >= 260) {
        lastBeepAt = now;
        if (alarmFlip)
          buzzerTone(2200);
        else
          buzzerOff();
      }
      ui.drawFlashIfNeeded();

      if (shortClick) {
        ui.uiTick(true);
        alarmStop();
        model.nextPhase();
        ui.drawTimerBase();
      }
      return;
    }

    if (shortClick) {
      ui.uiTick(true);
      if (!model.st.running)
        model.startTimer();
      else
        model.pauseTimer();
      ui.lastShownMin = -1; // Force redraw
    }

    if (step != 0) {
      uiHaptic(); // Sound only!

      // accel
      int mult = 1;
      if (now - lastEncStepAt < 70)
        mult = 3;
      else if (now - lastEncStepAt < 120)
        mult = 2;
      lastEncStepAt = now;

      int delta = step * 5 * mult;

      if (model.phase == PHASE_WORK) {
        int v = model.cfg.workMin + delta;
        if (v < 5)
          v = 5;
        if (v > 90)
          v = 90;
        model.cfg.workMin = v;
      } else {
        int v = model.cfg.breakMin + delta;
        if (v < 5)
          v = 5;
        if (v > 90)
          v = 90;
        model.cfg.breakMin = v;
      }
      model.save();
      model.resetTimer();
      ui.drawTimerBase();
    }

    if (model.st.running && model.remainingMs() == 0) {
      model.finishTimer();
      finishRitualActive = true;
      finishRitualStep = 0;
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

  if (screen == SCR_SETTINGS) {
    if (!ui.baseDrawn) {
      ui.drawSettings();
      ui.baseDrawn = true;
    }

    if (!ui.settingsEditing) {
      if (step != 0) {
        ui.settingsSel += step;
        if (ui.settingsSel < 0)
          ui.settingsSel = 5;
        if (ui.settingsSel > 5)
          ui.settingsSel = 0;
        ui.drawSettings();
      }
      if (shortClick) {
        ui.settingsEditing = true;
        ui.uiTick(true);
        ui.drawSettings();
      }
    } else {
      if (step != 0) {
        uiHaptic(); // sound only
        // ... Logic to edit value ...
        // Copying from original logic essentially but using model
        if (ui.settingsSel == 0)
          model.cfg.workMin =
              ui.model->clampU8(model.cfg.workMin + step * 5, 5, 90);
        else if (ui.settingsSel == 1)
          model.cfg.breakMin =
              ui.model->clampU8(model.cfg.breakMin + step * 5, 5, 90);
        else if (ui.settingsSel == 2)
          model.cfg.soundOn = !model.cfg.soundOn;
        else if (ui.settingsSel == 3) {
          model.cfg.screenBright =
              ui.model->clampU8(model.cfg.screenBright + step * 12, 20, 255);
          setBacklight(model.cfg.screenBright);
        } else if (ui.settingsSel == 4)
          model.cfg.ledBright =
              ui.model->clampU8(model.cfg.ledBright + step * 12, 0, 255);

        ui.drawSettings();
      }
      if (shortClick) {
        if (ui.settingsSel == 5) { // Theme
          model.cfg.darkMode = !model.cfg.darkMode;
          model.save();
          enterScreen(SCR_SETTINGS); // Re-enter to apply theme fully
          return;
        }
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
    return;
  }

  if (screen == SCR_STREAK) {
    if (!ui.baseDrawn) {
      ui.drawStreak();
      ui.baseDrawn = true;
    }

    if (!ui.streakEditing) {
      if (step != 0) {
        model.streakCursor += step;
        if (model.streakCursor < 0)
          model.streakCursor = STREAK_N - 1;
        if (model.streakCursor >= STREAK_N)
          model.streakCursor = 0;
        ui.drawStreak();
      }
      if (shortClick) {
        ui.streakEditing = true;
        model.currentDayIndex = model.streakCursor;
        ui.drawStreak();
      }
    } else {
      if (step != 0) {
        int v = model.streak[model.currentDayIndex] + step;
        if (v < 0)
          v = 0;
        if (v > 3)
          v = 3;
        model.streak[model.currentDayIndex] = v;
        ui.drawStreak();
      }
      if (shortClick) {
        ui.streakEditing = false;
        model.save();
        model.streakCursor = model.currentDayIndex;
        ui.drawStreak();
      }
    }
    return;
  }

  if (screen == SCR_CLOCK) {
    if (!ui.baseDrawn) {
      ui.drawClock();
      ui.baseDrawn = true;
    }

    if (!ui.clockEditing) {
      if (shortClick) {
        ui.clockEditing = true;
        ui.clockField = 0;
        ui.drawClock();
      }
      // Update clock
      static uint32_t lastC = 0;
      if (now - lastC >= 250) {
        lastC = now;
        ui.drawClock();
      }
    } else {
      if (step != 0) {
        if (ui.clockField == 0) {
          model.clockH += step;
          if (model.clockH < 0)
            model.clockH = 23;
          if (model.clockH > 23)
            model.clockH = 0;
        } else if (ui.clockField == 1) {
          model.clockM += step;
          if (model.clockM < 0)
            model.clockM = 59;
          if (model.clockM > 59)
            model.clockM = 0;
        } else {
          model.clockS += step;
          if (model.clockS < 0)
            model.clockS = 59;
          if (model.clockS > 59)
            model.clockS = 0;
        }
        model.clockSetAtMs = millis();
        ui.drawClock();
      }
      if (shortClick) {
        ui.clockField++;
        if (ui.clockField > 2) {
          ui.clockEditing = false;
          model.save();
        }
        ui.drawClock();
      }
    }
    return;
  }

  if (screen == SCR_WIFI_INFO) {
    if (!ui.baseDrawn) {
      ui.drawWifi(portal.wifiOn, portal.apSSID, portal.apPass,
                  WiFi.softAPIP().toString());
      ui.baseDrawn = true;
    }
    if (shortClick) {
      ui.uiTick(true);
      enterScreen(SCR_WIFI_QR);
    }
    return;
  }

  if (screen == SCR_WIFI_QR) {
    if (!ui.baseDrawn) {
      ui.drawQRFull(portal.apSSID, portal.apPass);
      ui.baseDrawn = true;
    }
    if (shortClick) {
      ui.uiTick(true);
      enterScreen(SCR_WIFI_INFO); // Handles theme restore and redraw
    }
    return;
  }
}