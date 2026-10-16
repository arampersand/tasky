#include "config_hardware.h"
#include "config_features.h"
#include "model.h"
#include "network_manager.h"
#include "ota_service.h"
#include "usb_setup.h"
#if FEATURE_BLE_HID
#include "ble_hid.h"
#endif
#if FEATURE_AGENTS
#include <ArduinoJson.h>
#endif
#include "ui.h"
#include "wifi_portal.h"
#if FEATURE_CALENDAR
#include "calendar_service.h"
#endif
#if FEATURE_GITHUB
#include "github_service.h"
#endif
#if FEATURE_NOTION
#include "notion_service.h"
#endif
#if FEATURE_FIREBASE
#include "firebase_service.h"
#endif
#if FEATURE_SPOTIFY
#include "spotify_service.h"
#endif
#if FEATURE_WEATHER
#include "weather_service.h"
#endif
#if FEATURE_LLM
#include "llm_service.h"
#endif
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <SPI.h>

// ==================== MODULES ====================
enum EncClickType { ENC_NONE, ENC_SHORT, ENC_LONG };

TaskyModel model;
TaskyUI ui(&model, TFT_CS, TFT_DC, TFT_RST);
TaskyNetManager net(&model);
#if FEATURE_CALENDAR
CalendarService calendar(&model);
#endif
#if FEATURE_GITHUB
GithubService github(&model);
#endif
#if FEATURE_NOTION
NotionService notion(&model);
#endif
#if FEATURE_FIREBASE
FirebaseService firebase(&model);
#endif
#if FEATURE_WEATHER
WeatherService weather(&model);
#endif
#if FEATURE_SPOTIFY
SpotifyService spotify(&model);
#endif
#if FEATURE_LLM
LlmService llm(&model);
#endif
OtaService ota(&model);
WiFiPortal portal(&model, &net, &ota);
UsbSetup usbSetup(&model, &net);
#if FEATURE_AGENTS
AgentBoard agents;
#endif

// ==================== STATE ====================
Screen screen = SCR_HOME;
Screen screenBeforeOverlay = SCR_HOME; // Where Ambient / notices return to

// Input state
bool lastEncBtn = HIGH;
uint32_t encBtnDownAt = 0;
bool lastModeBtn = HIGH;
const uint32_t ENC_LONGPRESS_MS = 900;
uint32_t lastInputAt = 0;

// Idle screen after this long without input (timer not running)
const uint32_t AMBIENT_IDLE_MS = 5UL * 60UL * 1000UL;
// Meeting alert lead time
const int MEETING_LEAD_MIN = 5;
// Wait for WiFi + calendar after boot before the morning/evening brief
const uint32_t BRIEF_BOOT_DELAY_MS = 2UL * 60UL * 1000UL;

// Notice overlay (meeting alert / morning brief / day close)
NoticeType noticeType = NOTICE_MEETING;
CalendarEventItem noticeEvent;
int noticeMinutes = 0;
bool noticeTimerPaused = false;

// Focus lock feedback on the timer task line
uint32_t lockToastUntil = 0;

enum ModeClickType { MODE_NONE, MODE_SHORT, MODE_LONG, MODE_DOUBLE };

uint32_t lastAnimAt = 0;
uint32_t lastBeepAt = 0;
uint32_t lastUiBeepAt = 0;
bool alarmFlip = false;
uint32_t uiBeepOffAt = 0;

// ==================== HARDWARE HELPERS ====================
// Light bar: PWM for plain LEDs, or an SK6812 chain driven by the RMT
// peripheral (10 MHz tick = 100 ns). setLed() runs every loop, so the RGB path
// only transmits when the level changes.
uint8_t lightMode = 0xFF;  // what LED_PIN is set up as (0xFF = not yet)
int16_t lightLevel = -1;

void initLight() {
  if (lightMode == model.cfg.ledType)
    return;
  if (lightMode == 0)
    ledcDetach(LED_PIN);
  else if (lightMode == 1)
    rmtDeinit(LED_PIN);
  if (model.cfg.ledType == 1)
    rmtInit(LED_PIN, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
  else
    ledcAttach(LED_PIN, 5000, 8);
  lightMode = model.cfg.ledType;
  lightLevel = -1;
}

void setLed(uint8_t v) {
  if (lightMode == 0) {
    ledcWrite(LED_PIN, v);
    return;
  }
  if (lightMode != 1 || v == lightLevel)
    return;
  lightLevel = v;
  static rmt_data_t bits[RGB_LED_COUNT * 24];
  const uint32_t rgb = RGB_LED_COLOR;
  const uint8_t grb[3] = {(uint8_t)(((rgb >> 8) & 0xFF) * v / 255),
                          (uint8_t)((rgb >> 16) * v / 255),
                          (uint8_t)((rgb & 0xFF) * v / 255)};
  int i = 0;
  for (int led = 0; led < RGB_LED_COUNT; led++)
    for (uint8_t c : grb)
      for (int b = 7; b >= 0; b--) {
        bool one = (c >> b) & 1;
        bits[i++] = {one ? 6u : 3u, 1u, one ? 6u : 9u, 0u};  // T1H/T0H, then low
      }
  rmtWrite(LED_PIN, bits, i, RMT_WAIT_FOR_EVER);
}

#if TFT_BL >= 0
void setBacklight(uint8_t v) {
  // Si TFT_BL comparte pin con buzzer: digitalWrite para restaurar tras buzzerOff
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, (v > 0) ? HIGH : LOW);
}
#else
void setBacklight(uint8_t) {}
#endif

// Active Buzzer Logic
void buzzerOff() {
#if ACTIVE_BUZZER
  digitalWrite(BUZZER_PIN, LOW);
#else
  ledcWriteTone(BUZZER_PIN, 0);
#endif
#if TFT_BL >= 0
  // Backlight puede compartir pin con buzzer - restaurar siempre encendido
  setBacklight(model.cfg.screenBright);
#endif
}

void buzzerTone(uint16_t hz) {
#if ACTIVE_BUZZER
  // Active buzzer: simple on/off, ignore frequency (except 0)
  if (hz > 0)
    digitalWrite(BUZZER_PIN, HIGH);
  else
    digitalWrite(BUZZER_PIN, LOW);
#else
  ledcWriteTone(BUZZER_PIN, hz);
#endif
}

void alarmStop() {
  buzzerOff();
  setLed(0);
}

// ==================== INPUT FUNCTIONS ====================
int encoderStep() {
  static int lastClk = HIGH;
  int clk = digitalRead(ENCODER_CLK);
  int dt = digitalRead(ENCODER_DT);
  if (clk != lastClk && clk == LOW) {
    lastClk = clk;
    return (dt == HIGH) ? -1 : +1;
  }
  lastClk = clk;
  return 0;
}

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

ModeClickType modeClickType() {
  static uint32_t modeDownAt = 0;
  static uint32_t lastShortClickAt = 0;
  static bool waitingForDouble = false;
  const uint32_t DOUBLE_CLICK_WINDOW = 400;  // ms

  bool b = digitalRead(MODO_BTN);

  // Button pressed
  if (b == LOW && lastModeBtn == HIGH) {
    modeDownAt = millis();
  }

  // Button released
  if (b == HIGH && lastModeBtn == LOW) {
    uint32_t press = millis() - modeDownAt;
    lastModeBtn = b;

    if (press < 80) return MODE_NONE;  // Debounce

    if (press >= 1500) {
      // Long press - reset double-click state
      waitingForDouble = false;
      return MODE_LONG;
    }

    // Short press - check for double-click
    uint32_t now = millis();
    if (waitingForDouble && (now - lastShortClickAt) < DOUBLE_CLICK_WINDOW) {
      // Second click within window = double-click!
      waitingForDouble = false;
      return MODE_DOUBLE;
    }

    // First click - start waiting for potential second click
    lastShortClickAt = now;
    waitingForDouble = true;
    return MODE_NONE;  // Don't return SHORT yet, wait for potential double
  }

  lastModeBtn = b;

  // Check if we're waiting for double-click and timeout expired
  if (waitingForDouble && (millis() - lastShortClickAt) >= DOUBLE_CLICK_WINDOW) {
    waitingForDouble = false;
    return MODE_SHORT;  // No second click came, it was a single click
  }

  return MODE_NONE;
}

// ==================== NAVIGATION & HANDLERS ====================
// MODO cycles through the everyday screens; everything else lives in More.
#if TASKY_EDITION_AGENTS
static const Screen CAROUSEL[] = {SCR_HOME,  SCR_AGENTS, SCR_CONTROLS, SCR_TIMER,
                                  SCR_TASKS, SCR_AGENDA, SCR_MORE};
#else
static const Screen CAROUSEL[] = {SCR_HOME,   SCR_TIMER, SCR_TASKS,
                                  SCR_AGENDA, SCR_STATS, SCR_MORE};
#endif
static const int CAROUSEL_N = sizeof(CAROUSEL) / sizeof(CAROUSEL[0]);

struct MoreItem {
  const char *label;
  Screen screen;
};
static const MoreItem MORE_ITEMS[] = {
    {"Focus", SCR_FOCUS},
    {"Calendar", SCR_CALENDAR},
    {"Streak", SCR_STREAK},
#if FEATURE_WEATHER
    {"Weather", SCR_WEATHER},
#endif
#if FEATURE_LLM
    {"Agent", SCR_AGENT},
#endif
#if FEATURE_SPOTIFY
    {"Music", SCR_SPOTIFY},
#endif
    {"Update", SCR_UPDATE},
    {"Setup (QR)", SCR_QR},
    {"Settings", SCR_SETTINGS},
    {"Tour", SCR_TOUR},
};
static const int MORE_N = sizeof(MORE_ITEMS) / sizeof(MORE_ITEMS[0]);
static const char *moreLabels[MORE_N];

static int carouselIndex(Screen s) {
  for (int i = 0; i < CAROUSEL_N; i++)
    if (CAROUSEL[i] == s)
      return i;
  return -1;
}

void goTo(Screen next) {
  if (next == SCR_QR || next == SCR_WIFI_INFO || next == SCR_UPDATE) {
    if (!portal.wifiOn) portal.begin();
  }
  if (model.st.finished && next != SCR_TIMER) {
    alarmStop();
  }
  screen = next;
  ui.baseDrawn = false;
  ui.settingsEditing = false;
  ui.streakEditing = false;
  ui.applyTheme();
}

// Screens opened from More return to More with MODO (either direction)
void navigateNext() {
  int i = carouselIndex(screen);
  goTo(i < 0 ? SCR_MORE : CAROUSEL[(i + 1) % CAROUSEL_N]);
}

void navigatePrev() {
  int i = carouselIndex(screen);
  goTo(i < 0 ? SCR_MORE : CAROUSEL[(i + CAROUSEL_N - 1) % CAROUSEL_N]);
}

void handleTimer(int step, EncClickType click) {
  uint32_t now = millis();
  if (!ui.baseDrawn) {
    ui.drawTimerBase();
    ui.baseDrawn = true;
  }
  if (model.st.finished) {
    if (click == ENC_SHORT) {
      ui.uiTick(true);
      alarmStop();
      model.nextPhase();
      ui.drawTimerBase();
    }
    if (click == ENC_LONG) {
      ui.uiTick(true);
      alarmStop();
      model.resetTimer();
      ui.drawTimerBase();
    }
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
    return;
  }
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
  if (step != 0 && !model.st.running) {
    int delta = -step * 5;
    if (model.phase == PHASE_WORK) {
      model.cfg.workMin = constrain(model.cfg.workMin + delta, 5, 90);
    } else {
      model.cfg.breakMin = constrain(model.cfg.breakMin + delta, 1, 30);
    }
    model.resetTimer();
    ui.drawTimerBase();
  }
  if (click == ENC_LONG) {
    ui.uiTick(true);
    model.resetTimer();
    ui.drawTimerBase();
  }
  if (model.st.running && model.remainingMs() == 0) {
    model.finishTimer();
    ui.drawFinishScreen("WELL DONE");
    ui.uiTick(true);
  }
  // Actualizar display solo cuando cambia el segundo (reduce parpadeo y CPU)
  static uint32_t lastTextAt = 0;
  if (now - lastTextAt >= 250) {
    lastTextAt = now;
    int mm = model.remainingMs() / 60000;
    int ss = (model.remainingMs() % 60000) / 1000;
    if (mm != ui.lastShownMin || ss != ui.lastShownSec) {
      ui.drawTimerTime(mm, ss);
      ui.lastShownMin = mm;
      ui.lastShownSec = ss;
    }
  }
}

void handleTasks(int step, EncClickType click) {
  static bool showingDetail = false;
  if (!ui.baseDrawn) {
    ui.drawTasks();
    ui.baseDrawn = true;
    showingDetail = false;
  }
  if (showingDetail) {
    // Click on an open task: make it the task the timer works on
    if (click == ENC_SHORT && !model.taskDone[ui.tasksSel]) {
      ui.uiTick(true);
      model.currentTask = ui.tasksSel;
      model.save();
      showingDetail = false;
      goTo(SCR_TIMER);
      return;
    }
    if (click != ENC_NONE) {
      showingDetail = false;
      ui.drawTasks();
    }
    return;
  }
  if (step != 0) {
    ui.tasksSel += step;
    if (ui.tasksSel < 0)
      ui.tasksSel = 3;
    if (ui.tasksSel > 3)
      ui.tasksSel = 0;
    ui.drawTasks();
  }
  if (click == ENC_SHORT) {
    if (ui.tasksSel == 3) {
      // "Add" item → go to QR screen
      screen = SCR_QR;
      ui.goToScreen(SCR_QR);
      if (!portal.wifiOn) portal.begin();
      return;
    }
    model.taskDone[ui.tasksSel] = !model.taskDone[ui.tasksSel];
    ui.uiTick(true);
    if (model.taskDone[ui.tasksSel]) {
      model.streakAddCompletion();
      if (model.currentTask == ui.tasksSel)
        model.currentTask = -1; // Done: stop tracking time on it
    }
    model.save();
    ui.drawTasks();
  }
  if (click == ENC_LONG && ui.tasksSel < 3) {
    showingDetail = true;
    ui.uiTick(true);
    ui.drawTaskDetail(ui.tasksSel);
  }
}

void handleCalendar(int step, EncClickType click) {
  static uint32_t dayTasksClosedAt = 0;  // Debounce guard

  if (ui.showingDayTasks) {
    if (click == ENC_SHORT || click == ENC_LONG) {
      ui.showingDayTasks = false;
      ui.baseDrawn = false;
      dayTasksClosedAt = millis();
    }
    return;
  }
  if (!ui.baseDrawn) {
    ui.drawCalendar();
    ui.baseDrawn = true;
  }
  int daysInMonth = 31;
  if (step != 0) {
    ui.calendarSelDay += step;
    if (ui.calendarSelDay < 1)
      ui.calendarSelDay = 31;
    if (ui.calendarSelDay > 31)
      ui.calendarSelDay = 1;
    ui.drawCalendar();
  }
  if (click == ENC_SHORT && ui.calendarSelDay > 0 && (millis() - dayTasksClosedAt > 300)) {
    ui.showingDayTasks = true;
    ui.uiTick(true);
    ui.drawDayTasks(ui.calendarSelDay);
  }
}

void handleStreak(int step, EncClickType click) {
  if (!ui.baseDrawn) {
    ui.drawStreak();
    ui.baseDrawn = true;
  }
  if (step != 0) {
    model.streakCursor = constrain(model.streakCursor + step, 0, STREAK_N - 1);
    ui.uiTick(false);
    ui.drawStreak();
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    model.streak[model.streakCursor] =
        (model.streak[model.streakCursor] + 1) % 4;
    model.save();
    ui.drawStreak();
  }
}

void handleStats(int step, EncClickType click) {
  if (!ui.baseDrawn) {
    ui.drawStats();
    ui.baseDrawn = true;
  }
}

void handleSettings(int step, EncClickType click) {
  const int maxSettingsIdx = 5;  // 0-5 (6 items)

  // Update WiFi status for display
  ui.wifiConnected = net.isConnected();

  if (!ui.baseDrawn) {
    ui.drawSettings();
    ui.baseDrawn = true;
  }
  if (ui.settingsEditing) {
    if (step != 0) {
      switch (ui.settingsSel) {
      case 0:
        model.cfg.workMin = constrain(model.cfg.workMin + step * 5, 5, 90);
        break;
      case 1:
        model.cfg.breakMin = constrain(model.cfg.breakMin + step * 5, 1, 30);
        break;
      case 2:
        // WiFi - no editing, handled by click
        break;
      case 3:
        model.cfg.screenBright =
            constrain(model.cfg.screenBright + step * 25, 20, 255);
        break;
      case 4:
        model.cfg.ledBright =
            constrain(model.cfg.ledBright + step * 25, 0, 255);
        break;
      case 5:
        model.cfg.darkMode = !model.cfg.darkMode;
        ui.applyTheme();
        break;
      }
      ui.drawSettings();
    }
    if (click == ENC_SHORT) {
      model.save();
      ui.settingsEditing = false;
      ui.uiTick(true);
      ui.drawSettings();
    }
  } else {
    if (step != 0) {
      ui.settingsSel += step;
      if (ui.settingsSel < 0)
        ui.settingsSel = maxSettingsIdx;
      if (ui.settingsSel > maxSettingsIdx)
        ui.settingsSel = 0;
      ui.drawSettings();
    }
    if (click == ENC_SHORT) {
      ui.uiTick(true);
      // WiFi item: navigate to WiFi screen instead of editing
      if (ui.settingsSel == 2) {
        if (!portal.wifiOn) portal.begin();
        screen = SCR_WIFI_INFO;
        ui.baseDrawn = false;
        return;
      }
      ui.settingsEditing = true;
      ui.drawSettings();
    }
  }
}

void handleQR(int step, EncClickType click) {
  if (!ui.baseDrawn) {
    if (!portal.wifiOn)
      portal.begin();
    ui.drawQRFull(portal.apSSID, model.cfg.apPass);
    ui.baseDrawn = true;
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    screen = SCR_HOME;
    ui.baseDrawn = false;
  }
}

void handleWifi(int step, EncClickType click) {
  if (!ui.baseDrawn) {
    if (!portal.wifiOn)
      portal.begin();
    String ip = net.isConnected() ? net.getIP() : WiFi.softAPIP().toString();
    if (ip == "0.0.0.0")
      ip = WiFi.softAPIP().toString();
    String status = "Status: " + net.getStatusString();
    ui.drawWifi(portal.wifiOn, status, model.cfg.apPass, ip);
    ui.baseDrawn = true;
  }
  static String lastStatus = "";
  if (net.getStatusString() != lastStatus) {
    lastStatus = net.getStatusString();
    ui.baseDrawn = false;
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    // Go back to Settings
    screen = SCR_SETTINGS;
    ui.baseDrawn = false;
  }
}

void handleFocus(int step, EncClickType click) {
  if (!ui.baseDrawn) {
    ui.drawFocus();
    ui.baseDrawn = true;
  }
  if (step != 0) {
    ui.focusSel = (ui.focusSel + step + 3) % 3;
    ui.drawFocus();
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    Config &c = model.cfg;
    if (ui.focusSel == 0)
      c.focusAutoMode = !c.focusAutoMode;
    else if (ui.focusSel == 1)
      c.focusLight = !c.focusLight;
    else
      c.focusLock = !c.focusLock;
    model.save();
    ui.drawFocus();
  }
}

void handleMore(int step, EncClickType click) {
  if (!ui.baseDrawn) {
    ui.drawMore(moreLabels, MORE_N);
    ui.baseDrawn = true;
  }
  if (step != 0) {
    ui.moreSel = (ui.moreSel + step + MORE_N) % MORE_N;
    ui.drawMore(moreLabels, MORE_N);
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    goTo(MORE_ITEMS[ui.moreSel].screen);
  }
}

// Firmware uploads are refused unless allowed here (physical confirmation)
void handleUpdate(int step, EncClickType click) {
  static bool shownArmed = false;
  static uint32_t lastTickAt = 0;
  if (!ui.baseDrawn || ota.isArmed() != shownArmed) {
    shownArmed = ota.isArmed();
    String url, hint;
    if (net.isConnected()) {
      url = "http://" + net.getIP() + "/";
      hint = "Login: tasky / " + model.cfg.apPass;
    } else {
      url = "http://" + WiFi.softAPIP().toString() + "/";
      hint = "WiFi " + portal.apSSID + " / " + model.cfg.apPass;
    }
    ui.drawUpdate(shownArmed, url, hint, ota.getVersion());
    ui.baseDrawn = true;
    lastTickAt = 0;
  }
  if (shownArmed && millis() - lastTickAt >= 1000) {
    lastTickAt = millis();
    ui.drawUpdateCountdown(ota.armedRemainingMs());
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    if (ota.isArmed())
      ota.disarm();
    else
      ota.arm();
    ui.baseDrawn = false;
  }
}

// ==================== OVERLAYS ====================
void drawAmbientScreen() {
#if FEATURE_WEATHER
  ui.drawAmbient(weather.hasData(), weather.temperature, weather.description,
                 model.cfg.weatherCelsius);
#else
  ui.drawAmbient(false, 0, "", true);
#endif
}

void enterAmbient() {
  screenBeforeOverlay = screen;
  screen = SCR_AMBIENT;
  ui.baseDrawn = false;
}

// Home and Ambient share the glance layout. Full redraw every 5 min or when
// weather first arrives; clock and phrase every minute (no flicker).
void refreshGlance() {
  static int lastMin = -1;
  static uint32_t lastFullAt = 0;
  static bool hadWeather = false;
#if FEATURE_WEATHER
  bool haveWeather = weather.hasData();
#else
  bool haveWeather = false;
#endif
  struct tm t;
  bool hasTime = model.localNow(t);
  int minuteKey = hasTime ? t.tm_min : (int)(millis() / 60000UL % 60);
  if (!ui.baseDrawn || millis() - lastFullAt > 5UL * 60UL * 1000UL ||
      haveWeather != hadWeather) {
    drawAmbientScreen();
    ui.baseDrawn = true;
    lastFullAt = millis();
    lastMin = minuteKey;
    hadWeather = haveWeather;
  } else if (minuteKey != lastMin) {
    lastMin = minuteKey;
    ui.drawAmbientClock();
    ui.drawAmbientPhrase();
  }
}

void handleAmbient() { refreshGlance(); }

// Home: press = start focusing, long press = tasks
void handleHome(EncClickType click) {
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    goTo(SCR_TIMER);
    if (!model.st.running && !model.st.finished) {
      model.save();
      model.startTimer();
      ui.lastShownMin = -1;
    }
    return;
  }
  if (click == ENC_LONG) {
    ui.uiTick(true);
    goTo(SCR_TASKS);
    return;
  }
  refreshGlance();
}

void showNotice(NoticeType type) {
  if (screen == SCR_TOUR)
    return;  // don't interrupt the first-boot tour
  // Keep the original screen when a notice lands on top of Ambient
  if (screen != SCR_AMBIENT && screen != SCR_NOTICE)
    screenBeforeOverlay = screen;
  noticeType = type;
  screen = SCR_NOTICE;
  ui.baseDrawn = false;
}

// ==================== TOUR & HINTS ====================
// First boot: a 4-step tour that waits for the action each step teaches.
// Afterwards each screen shows a one-line hint on its first HINT_VISITS visits.
// Both live in the "tasky" NVS namespace, so a factory reset brings them back.
const int HINT_VISITS = 3;
const uint32_t HINT_MS = 3000;
uint8_t hintVisits[SCR_NOTICE + 1];
uint32_t hintUntil = 0;
int tourStep = 0;

const char *hintFor(Screen s) {
  switch (s) {
  case SCR_HOME:     return "Press: focus  Hold: tasks";
  case SCR_TIMER:    return "Turn: time  Press: start";
  case SCR_TASKS:    return "Press: done  Hold: info";
  case SCR_AGENDA:   return "Press: refresh";
  case SCR_STATS:    return "MODE: next screen";
  case SCR_MORE:     return "Turn: pick  Press: open";
  case SCR_FOCUS:    return "Turn: pick  Press: on/off";
  case SCR_CALENDAR: return "Turn: day  Press: open";
  case SCR_STREAK:   return "Turn: day  Press: mark";
  case SCR_SETTINGS: return "Press: edit  Turn: change";
#if FEATURE_AGENTS
  case SCR_AGENTS:   return "Press: seen  Hold: clear";
#endif
  default:           return nullptr;  // screens that already explain themselves
  }
}

// Returns true when the tour should run (first boot of a new device)
bool loadTourAndHints() {
  Preferences p;
  p.begin("tasky", true);
  bool tourDone = p.getBool("tour", false);
  memset(hintVisits, 0, sizeof(hintVisits));
  if (p.isKey("hints"))
    p.getBytes("hints", hintVisits, sizeof(hintVisits));
  p.end();
  // Devices upgraded from 2.8 or older are already in use: skip the tour
  return !tourDone && model.cfg.wifiSSID.length() == 0 && model.cfg.totalFocusMin == 0;
}

void endTour() {
  Preferences p;
  p.begin("tasky", false);
  p.putBool("tour", true);
  p.end();
  tourStep = 0;
  goTo(SCR_HOME);
}

void handleTour(int step, EncClickType click, ModeClickType mode) {
  if (!ui.baseDrawn) {
    ui.drawTour(tourStep);
    ui.baseDrawn = true;
  }
  if (mode == MODE_LONG) {
    endTour();
    return;
  }
  bool done = (tourStep == 0 && step != 0) ||
              ((tourStep == 1 || tourStep == 3) && click != ENC_NONE) ||
              (tourStep == 2 && (mode == MODE_SHORT || mode == MODE_DOUBLE));
  if (!done)
    return;
  ui.uiTick(true);
  if (tourStep == 3) {
    endTour();
    return;
  }
  tourStep++;
  ui.baseDrawn = false;
}

// Before the screen handler: hide the hint on input or timeout. Handlers draw
// their base before reading input, so the redraw never undoes the action.
void hideHint(bool anyInput) {
  if (hintUntil && (anyInput || millis() > hintUntil)) {
    hintUntil = 0;
    ui.baseDrawn = false;  // redraw what the hint covered
    ui.lastShownMin = -1;
  }
}

// After the screen handler, so the hint lands on top of a drawn screen
void showHint() {
  static Screen hintScreen = SCR_TOUR;
  if (screen == hintScreen || !ui.baseDrawn)
    return;
  hintScreen = screen;
  hintUntil = 0;  // the previous screen's hint went away with it
  const char *hint = hintFor(screen);
  if (!hint || hintVisits[screen] >= HINT_VISITS)
    return;
  hintVisits[screen]++;
  Preferences p;
  p.begin("tasky", false);
  p.putBytes("hints", hintVisits, sizeof(hintVisits));
  p.end();
  ui.drawHint(hint);
  hintUntil = millis() + HINT_MS;
}

void handleNotice(EncClickType click, bool modePressed) {
  static uint32_t shownAt = 0;
  if (!ui.baseDrawn) {
    if (noticeType == NOTICE_MEETING)
      ui.drawMeetingAlert(noticeEvent, noticeMinutes, noticeTimerPaused);
    else if (noticeType == NOTICE_MORNING)
      ui.drawMorningBrief();
    else
      ui.drawDayClose();
    ui.baseDrawn = true;
    shownAt = millis();
  }

  // Meeting: blink the light bar for 10 s, three beeps at the start
  if (noticeType == NOTICE_MEETING) {
    uint32_t el = millis() - shownAt;
    bool on = (el / 250) % 2 == 0;
    setLed((el < 10000 && on) ? model.cfg.ledBright : 0);
    if (model.cfg.soundOn && el < 1500) {
      if (on) buzzerTone(2200);
      else buzzerOff();
    } else if (el < 1800) {
      buzzerOff();
    }
  }

  if (click != ENC_NONE || modePressed) {
    alarmStop();
    screen = screenBeforeOverlay;
    ui.baseDrawn = false;
  }
}

// Alert once per event, MEETING_LEAD_MIN before it starts
void checkMeetingAlert() {
  if (screen == SCR_NOTICE || screen == SCR_UPDATE)
    return;
  struct tm t;
  if (!model.localNow(t))
    return;
  int nowMin = t.tm_hour * 60 + t.tm_min;
  const CalendarEventItem *ev = model.nextEventToday(nowMin);
  if (!ev)
    return;
  int minutes = TaskyModel::hhmmToMinutes(ev->startTime) - nowMin;
  if (minutes > MEETING_LEAD_MIN)
    return;

  static char lastKey[48] = "";
  char key[48];
  snprintf(key, sizeof(key), "%d|%s|%s", t.tm_yday, ev->startTime, ev->title);
  if (strcmp(key, lastKey) == 0)
    return;
  strcpy(lastKey, key);

  noticeEvent = *ev;
  noticeMinutes = minutes;
  noticeTimerPaused = model.st.running && model.phase == PHASE_WORK;
  if (noticeTimerPaused)
    model.pauseTimer();
  showNotice(NOTICE_MEETING);
}

// Morning brief at the start of the workday, day close at its end.
// Never interrupts a running session.
void checkDayBriefs() {
  if (millis() < BRIEF_BOOT_DELAY_MS || screen == SCR_NOTICE ||
      screen == SCR_UPDATE || model.st.running || model.st.finished)
    return;
  struct tm t;
  if (!model.localNow(t))
    return;
  Config &c = model.cfg;
  int h = t.tm_hour;
  if (h >= c.dayStartHour && h < c.dayEndHour && c.lastMorningDay != t.tm_yday) {
    c.lastMorningDay = t.tm_yday;
    model.save();
    showNotice(NOTICE_MORNING);
  } else if (h >= c.dayEndHour && c.lastEveningDay != t.tm_yday) {
    c.lastEveningDay = t.tm_yday;
    model.save();
    showNotice(NOTICE_EVENING);
  }
}

// Light bar: steady "do not disturb" while focusing. The finished alarm and
// the meeting alert drive it themselves.
void updateFocusLight() {
  if (screen == SCR_NOTICE || model.st.finished)
    return;
#if FEATURE_AGENTS
  if (agents.waitingCount(true) > 0) {  // breathe while an agent waits for you
    float phase = (millis() % 1600) / 1600.0f;
    float level = 0.15f + 0.85f * (0.5f - 0.5f * cosf(phase * 2 * PI));
    setLed((uint8_t)(model.cfg.ledBright * level));
    return;
  }
#endif
  bool on = model.isFocusModeActive() && model.cfg.focusLight;
  setLed(on ? model.cfg.ledBright : 0);
}
#include "agents_screens.h"


void handleAgenda(int step, EncClickType click) {
  static int lastEventCount = -1;

  if (!ui.baseDrawn) {
    ui.drawAgenda();
    ui.baseDrawn = true;
    lastEventCount = model.monthEventCount;
  }

  // Auto-refresh when calendar data arrives (was 0, now has events)
  if (model.monthEventCount != lastEventCount) {
    lastEventCount = model.monthEventCount;
    ui.baseDrawn = false;
  }

  // Refresh on click - force calendar + notion update
  if (click == ENC_SHORT) {
    ui.uiTick(true);
#if FEATURE_CALENDAR
    if (model.cfg.calendarUrl.length() > 0)
      calendar.forceUpdate();
#endif
#if FEATURE_NOTION
    if (model.cfg.notionApiKey.length() > 0)
      notion.forceUpdate();
#endif
    ui.baseDrawn = false;
  }
}

#if FEATURE_WEATHER
void handleWeather(int step, EncClickType click) {
  if (!ui.baseDrawn) {
    ui.drawWeather(
      weather.temperature,
      weather.feelsLike,
      weather.humidity,
      (uint8_t)weather.icon,
      weather.description,
      weather.cityName,
      model.cfg.weatherCelsius
    );
    ui.baseDrawn = true;
  }

  // Refresh on click
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    weather.forceUpdate();
    ui.baseDrawn = false;
  }
}
#endif

#if FEATURE_SPOTIFY
void handleSpotify(int step, EncClickType click) {
  if (!ui.baseDrawn) {
    ui.drawSpotify(
      spotify.playback.trackName,
      spotify.playback.artistName,
      spotify.playback.progressMs,
      spotify.playback.durationMs,
      spotify.playback.isPlaying,
      spotify.playback.volumePercent,
      spotify.playback.isActive
    );
    ui.baseDrawn = true;
  }

  // Volume control with encoder
  if (step != 0 && spotify.playback.isActive) {
    int newVol = constrain(spotify.playback.volumePercent + step * 5, 0, 100);
    spotify.setVolume(newVol);
    spotify.playback.volumePercent = newVol;  // Optimistic update
    ui.baseDrawn = false;
  }

  // Play/Pause on short click
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    if (spotify.playback.isActive) {
      if (spotify.playback.isPlaying) {
        spotify.pause();
        spotify.playback.isPlaying = false;
      } else {
        spotify.play();
        spotify.playback.isPlaying = true;
      }
    }
    ui.baseDrawn = false;
  }

  // Next track on long click
  if (click == ENC_LONG) {
    ui.uiTick(true);
    if (spotify.playback.isActive) {
      spotify.next();
      spotify.forceUpdate();
    }
    ui.baseDrawn = false;
  }

  // Update display periodically when playing
  static uint32_t lastSpotifyUpdate = 0;
  if (spotify.playback.isPlaying && millis() - lastSpotifyUpdate >= 1000) {
    lastSpotifyUpdate = millis();
    // Estimate progress locally between API updates
    spotify.playback.progressMs += 1000;
    if (spotify.playback.progressMs > spotify.playback.durationMs) {
      spotify.playback.progressMs = spotify.playback.durationMs;
    }
    ui.baseDrawn = false;
  }
}
#endif

#if FEATURE_LLM
void handleAgent(int step, EncClickType click) {
  // Mode selection overlay
  if (ui.agentModeSelect) {
    if (!ui.baseDrawn) {
      ui.drawAgentModeSelect(ui.agentModeSel, model.cfg.llmModeOverride);
      ui.baseDrawn = true;
    }
    if (step != 0) {
      ui.agentModeSel += step;
      if (ui.agentModeSel < 0) ui.agentModeSel = LLM_MODE_COUNT;
      if (ui.agentModeSel > LLM_MODE_COUNT) ui.agentModeSel = 0;
      ui.baseDrawn = false;
    }
    if (click == ENC_SHORT) {
      ui.uiTick(true);
      // Selection 0=AUTO (-1), 1-7=modes (0-6)
      int8_t newMode = (ui.agentModeSel == 0) ? -1 : (int8_t)(ui.agentModeSel - 1);
      model.cfg.llmModeOverride = newMode;
      model.save();
      ui.agentModeSelect = false;
      ui.baseDrawn = false;
    }
    if (click == ENC_LONG) {
      // Cancel, go back
      ui.agentModeSelect = false;
      ui.baseDrawn = false;
    }
    return;
  }

  AgentState agentState = llm.getState();
  LlmMode mode = llm.getTodayMode();
  const char* modeName = LlmService::getModeName(mode);
  const char* modeDesc = LlmService::getModeDesc(mode);

  if (!ui.baseDrawn) {
    ui.drawAgent((int)agentState, llm.response, llm.responseLen,
                 llm.errorMsg, modeName, modeDesc,
                 ui.agentShowingResponse, ui.agentScrollOffset);
    ui.baseDrawn = true;
  }

  // THINKING - refresh for animation, allow cancel
  if (agentState == AGENT_SENDING || agentState == AGENT_THINKING) {
    static uint32_t lastAgentDraw = 0;
    if (millis() - lastAgentDraw >= 400) {
      lastAgentDraw = millis();
      ui.baseDrawn = false;
    }
    if (click == ENC_SHORT) {
      llm.cancel();
      ui.agentShowingResponse = false;
      ui.baseDrawn = false;
    }
    return;
  }

  // DONE - scroll response, press to go back
  if (agentState == AGENT_DONE && ui.agentShowingResponse) {
    if (step != 0) {
      ui.agentScrollOffset += step * 18;
      if (ui.agentScrollOffset < 0) ui.agentScrollOffset = 0;
      ui.baseDrawn = false;
    }
    if (click == ENC_SHORT || click == ENC_LONG) {
      llm.reset();
      ui.agentShowingResponse = false;
      ui.agentScrollOffset = 0;
      ui.baseDrawn = false;
    }
    return;
  }

  // ERROR - press to go back to idle
  if (agentState == AGENT_ERROR) {
    if (click == ENC_SHORT) {
      llm.reset();
      ui.baseDrawn = false;
    }
    return;
  }

  // IDLE - short press = get today's insight, long press = mode selector
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    ui.agentShowingResponse = true;
    ui.agentScrollOffset = 0;

    // Show thinking animation before calling LLM (even for cache hits)
    LlmMode preMode = llm.getTodayMode();
    const char* preModeName = LlmService::getModeName(preMode);
    const char* preModeDesc = LlmService::getModeDesc(preMode);
    ui.drawAgent(AGENT_THINKING, nullptr, 0, nullptr, preModeName, preModeDesc, false, 0);
    delay(800);  // Brief animation so it feels like AI is processing

    llm.sendDailyPrompt();
    ui.baseDrawn = false;
  }
  if (click == ENC_LONG) {
    ui.uiTick(true);
    ui.agentModeSelect = true;
    // Pre-select current mode
    ui.agentModeSel = (model.cfg.llmModeOverride < 0) ? 0 : (model.cfg.llmModeOverride + 1);
    ui.baseDrawn = false;
  }
}
#endif

// ==================== ULTIMATE SCANNER ====================
#if DISPLAY_BRINGUP
void runDisplayBringup() {
  Serial.println("\n--- RUNNING DISPLAY SCANNER ---");

  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(50);
  digitalWrite(TFT_RST, LOW);
  delay(100);
  digitalWrite(TFT_RST, HIGH);
  delay(100);

  TaskyST7789 testTft(TFT_CS, TFT_DC, TFT_RST);
  // Using generic 240x280 init
  testTft.init(240, 280);
  testTft.setRotation(2);

  while (1) {
    // SCENARIO 1: Offset 0, Invert TRUE (Standard)
    Serial.println("SCAN: Offset 0, Invert TRUE -> RED");
    testTft.setOffsets(0, 0);
    testTft.invertDisplay(true);
    testTft.fillScreen(ST77XX_RED);
    testTft.setCursor(10, 50);
    testTft.setTextSize(3);
    testTft.setTextColor(ST77XX_WHITE);
    testTft.print("OFF: 0");
    delay(1000);

    // SCENARIO 2: Offset 20 (Y), Invert TRUE -> GREEN
    Serial.println("SCAN: Offset 20, Invert TRUE -> GREEN");
    testTft.setOffsets(0, 20);
    testTft.invertDisplay(true);
    testTft.fillScreen(ST77XX_GREEN);
    testTft.setCursor(10, 50);
    testTft.print("OFF: 20");
    delay(1000);

    // SCENARIO 3: Offset 80 (Y), Invert TRUE -> BLUE
    Serial.println("SCAN: Offset 80, Invert TRUE -> BLUE");
    testTft.setOffsets(0, 80); // Common for 1.69" cut from larger panel
    testTft.invertDisplay(true);
    testTft.fillScreen(ST77XX_BLUE);
    testTft.setCursor(10, 50);
    testTft.print("OFF: 80");
    delay(1000);

    // SCENARIO 4: Check Inversion (White Screen)
    Serial.println("SCAN: INVERSION CHECK -> WHITE/BLACK");
    testTft.setOffsets(0, 0);
    testTft.invertDisplay(false); // Toggle invert
    testTft.fillScreen(ST77XX_WHITE);
    delay(500);
    testTft.fillScreen(ST77XX_BLACK);
    delay(500);
  }
}
#endif

// ==================== SETUP ====================
void setup() {
  delay(500);   // Estabilizar alimentacion antes de init
  Serial.begin(115200);
  delay(500);   // Dar tiempo al Serial
  Serial.println("Booting TaskyFirmware...");
  Serial.printf("Pins: CS=%d DC=%d RST=%d MOSI=%d SCK=%d\n", TFT_CS, TFT_DC,
                TFT_RST, TFT_MOSI, TFT_SCK);
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());

  // Initialize SPI - velocidad controlada por tft.setSPISpeed() en ui.init()
  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);

  // Input pins
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);
  pinMode(MODO_BTN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
#if TFT_BL >= 0
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);  // Backlight on desde el inicio
#endif

  // ledcAttach(BUZZER_PIN, 2000, 8); // Removed for Active Buzzer

  buzzerTone(1000);
  delay(50);
  buzzerOff();

#if DISPLAY_BRINGUP
  runDisplayBringup();
#endif

  Serial.println("Loading model...");
  model.begin();
  initLight();  // needs the saved light type
  Serial.println("Init display...");
  ui.init();

  setLed(0);
  buzzerOff();
#if TFT_BL >= 0
  setBacklight(model.cfg.screenBright);
#endif
  model.clockSetAtMs = millis();
  Serial.println("Splash...");
  ui.drawSplashTasky();
  delay(1300);

  Serial.println("Starting services...");
  net.begin();
#if FEATURE_CALENDAR
  calendar.begin();
#endif
#if FEATURE_GITHUB
  github.begin();
#endif
#if FEATURE_NOTION
  notion.begin();
#endif
#if FEATURE_FIREBASE
  firebase.begin();
#endif
#if FEATURE_WEATHER
  weather.begin();
#endif
#if FEATURE_SPOTIFY
  spotify.begin();
#endif
#if FEATURE_LLM
  llm.begin();
#endif
  ota.begin();

  for (int i = 0; i < MORE_N; i++)
    moreLabels[i] = MORE_ITEMS[i].label;
  lastInputAt = millis();

  screen = loadTourAndHints() ? SCR_TOUR : SCR_HOME;
  ui.baseDrawn = false;
  ui.applyTheme();

#if FEATURE_BLE_HID
  BleHid::begin("Tasky");
#endif
#if FEATURE_AGENTS
  usbSetup.onCommand = handleAgentCommand;
#endif
  usbSetup.onChanged = []() {
    if (usbSetup.lightSet) {  // switch driver and blink so the user sees it work
      usbSetup.lightSet = false;
      initLight();
      setLed(model.cfg.ledBright);
      delay(400);
      setLed(0);
    }
#if FEATURE_WEATHER
    weather.requestRefresh();
#endif
#if FEATURE_CALENDAR
    calendar.requestRefresh();
#endif
    if (screen == SCR_HOME)
      ui.baseDrawn = false;  // show the new setup right away
  };
  Serial.println("Setup complete.");
}

// ==================== MAIN LOOP ====================
void loop() {
  yield();  // Evitar watchdog y dar tiempo a tareas WiFi
  uint32_t now = millis();
  // Setup screens keep the setup network up; it stops on its own when idle.
  if (screen == SCR_QR || screen == SCR_WIFI_INFO || screen == SCR_UPDATE)
    portal.touch();
  portal.loop();
  usbSetup.loop();
  net.loop();

#if TFT_BL >= 0
  // Mantener backlight encendido (evita que se apague tras inactividad)
  static uint32_t lastBlAt = 0;
  if (now - lastBlAt >= 2000) {
    lastBlAt = now;
    setBacklight(model.cfg.screenBright);
  }
#endif

  static uint32_t lastServiceCheck = 0;
  if (now - lastServiceCheck > 5000) {
    lastServiceCheck = now;
#if FEATURE_CALENDAR
    calendar.loop();
#endif
#if FEATURE_GITHUB
    github.loop();
#endif
#if FEATURE_NOTION
    notion.loop();
#endif
#if FEATURE_FIREBASE
    firebase.loop();
#endif
#if FEATURE_WEATHER
    weather.loop();
#endif
#if FEATURE_SPOTIFY
    spotify.loop();
#endif
#if FEATURE_LLM
    llm.loop();
#endif
    model.checkDayChange();
    checkMeetingAlert();
    checkDayBriefs();
  }

#if FEATURE_AGENTS
  static uint32_t lastExpireAt = 0;
  if (now - lastExpireAt > 60000UL) {
    lastExpireAt = now;
    agents.expire();
  }
  if (agents.changed) {
    agents.changed = false;
    refreshAgentStatus();
    if (screen == SCR_AGENTS)
      ui.baseDrawn = false;
    else if (screen == SCR_HOME || screen == SCR_AMBIENT)
      ui.drawAmbientPhrase();
  }
#endif

  static uint32_t lastLightAt = 0;
  if (now - lastLightAt >= 50) {  // 50 ms: smooth "agent waiting" breathing
    lastLightAt = now;
    updateFocusLight();
  }

  if (uiBeepOffAt && millis() >= uiBeepOffAt) {
    buzzerOff();
    uiBeepOffAt = 0;
  }

  ModeClickType modeClick = modeClickType();
  int step = encoderStep();
  EncClickType click = encoderClickType();

  bool anyInput = step != 0 || click != ENC_NONE || modeClick != MODE_NONE;
  if (anyInput)
    lastInputAt = now;

  static uint32_t bothBtnDownAt = 0;
  bool encDown = digitalRead(ENCODER_SW) == LOW;
  bool modeDown = digitalRead(MODO_BTN) == LOW;

  if (encDown && modeDown) {
    if (bothBtnDownAt == 0) {
      bothBtnDownAt = millis();
    } else if (millis() - bothBtnDownAt >= 3000) {
      ui.tft.fillScreen(0x0000);
      ui.tft.setTextColor(0xF800);
      ui.tft.setFont(&FreeSansBold12pt7b);
      ui.tft.setCursor(70, 110);
      ui.tft.print("RESET");
      ui.tft.setFont();
      delay(500);
      Preferences pref;
      pref.begin("tasky", false);
      pref.clear();
      pref.end();
      WiFi.disconnect(true, true);  // also erase the WiFi driver's saved credentials
      delay(500);
      ESP.restart();
    }
  } else {
    bothBtnDownAt = 0;
  }

  if (screen == SCR_TOUR) {
    handleTour(step, click, modeClick);
    return;
  }

  // Overlays take any input to close
  if (screen == SCR_NOTICE) {
    handleNotice(click, modeClick != MODE_NONE);
    return;
  }
  if (screen == SCR_AMBIENT) {
    if (anyInput) {
      screen = screenBeforeOverlay;
      ui.baseDrawn = false;
      return;
    }
    handleAmbient();
    return;
  }

  // Focus lock: MODO can't leave the timer while a focus session runs
  if (model.cfg.focusLock && model.isFocusModeActive() && screen == SCR_TIMER &&
      modeClick != MODE_NONE) {
    ui.uiTick(false);
    ui.drawTimerTaskLine("Pause to unlock");
    lockToastUntil = now + 1500;
    modeClick = MODE_NONE;
  }
  if (lockToastUntil && now > lockToastUntil) {
    lockToastUntil = 0;
    if (screen == SCR_TIMER)
      ui.drawTimerTaskLine();
  }

  // Idle: hold MODO, or 5 min without input while no session is running
  bool sessionActive = model.st.running || model.st.finished;
  bool idleAllowed = !sessionActive && screen != SCR_UPDATE &&
                     screen != SCR_QR && screen != SCR_WIFI_INFO &&
                     screen != SCR_HOME;  // Home already is the calm screen
  if (modeClick == MODE_LONG) {
    if (idleAllowed)
      enterAmbient();
    return;
  }
  if (idleAllowed && now - lastInputAt > AMBIENT_IDLE_MS) {
    enterAmbient();
    return;
  }

  if (modeClick == MODE_SHORT) {
    navigateNext();
    return;
  }

  if (modeClick == MODE_DOUBLE) {
    navigatePrev();
    return;
  }

  hideHint(anyInput);
  switch (screen) {
  case SCR_HOME:
    handleHome(click);
    break;
#if FEATURE_AGENTS
  case SCR_AGENTS:
    handleAgents(step, click);
    break;
#endif
#if FEATURE_BLE_HID
  case SCR_CONTROLS:
    handleControls(step, click);
    break;
#endif
  case SCR_TIMER:
    handleTimer(step, click);
    break;
  case SCR_MORE:
    handleMore(step, click);
    break;
  case SCR_FOCUS:
    handleFocus(step, click);
    break;
  case SCR_UPDATE:
    handleUpdate(step, click);
    break;
  case SCR_TASKS:
    handleTasks(step, click);
    break;
  case SCR_AGENDA:
    handleAgenda(step, click);
    break;
  case SCR_CALENDAR:
    handleCalendar(step, click);
    break;
  case SCR_STREAK:
    handleStreak(step, click);
    break;
  case SCR_STATS:
    handleStats(step, click);
    break;
#if FEATURE_WEATHER
  case SCR_WEATHER:
    handleWeather(step, click);
    break;
#endif
#if FEATURE_SPOTIFY
  case SCR_SPOTIFY:
    handleSpotify(step, click);
    break;
#endif
#if FEATURE_LLM
  case SCR_AGENT:
    handleAgent(step, click);
    break;
#endif
  case SCR_SETTINGS:
    handleSettings(step, click);
    break;
  case SCR_QR:
    handleQR(step, click);
    break;
  case SCR_WIFI_INFO:
    handleWifi(step, click);
    break;
  default:
    break;
  }
  showHint();
}
