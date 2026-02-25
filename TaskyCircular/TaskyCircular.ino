#include "config_hardware.h"
#include "config_features.h"
#include "model.h"
#include "network_manager.h"
#include "ota_service.h"
#include "power_manager.h"
#include "focus_dns.h"
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
PowerManager power(&model, TFT_RST, ENCODER_SW);
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
FocusDNS focusDns(&model);
WiFiPortal portal(&model, &net, &focusDns, &ota);

// ==================== STATE ====================
Screen screen = SCR_TIMER;
Screen screenBeforeScreensaver = SCR_TIMER;

// Input state
bool lastEncBtn = HIGH;
uint32_t encBtnDownAt = 0;
bool lastModeBtn = HIGH;
const uint32_t ENC_LONGPRESS_MS = 900;

bool screensaverActive = false;
enum ModeClickType { MODE_NONE, MODE_SHORT, MODE_LONG, MODE_DOUBLE };

uint32_t lastAnimAt = 0;
uint32_t lastBeepAt = 0;
uint32_t lastUiBeepAt = 0;
bool alarmFlip = false;
uint32_t uiBeepOffAt = 0;

// ==================== HARDWARE HELPERS ====================
void setLed(uint8_t v) { ledcWrite(LED_PIN, v); }

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
static Screen getNextScreen(Screen s) {
  // Order: Timer→Tasks→Calendar→Agenda→Agent→Focus→Streak→Weather→Stats→Spotify→QR→Settings
  switch (s) {
  case SCR_TIMER: return SCR_TASKS;
  case SCR_TASKS: return SCR_CALENDAR;
  case SCR_CALENDAR: return SCR_AGENDA;
  case SCR_AGENDA:
#if FEATURE_LLM
    return SCR_AGENT;
#else
    return SCR_FOCUS;
#endif
  case SCR_AGENT: return SCR_FOCUS;
  case SCR_FOCUS: return SCR_STREAK;
  case SCR_STREAK:
#if FEATURE_WEATHER
    return SCR_WEATHER;
#else
    return SCR_STATS;
#endif
  case SCR_WEATHER: return SCR_STATS;
  case SCR_STATS:
#if FEATURE_SPOTIFY
    return SCR_SPOTIFY;
#else
    return SCR_QR;
#endif
  case SCR_SPOTIFY: return SCR_QR;
  case SCR_QR: return SCR_SETTINGS;
  case SCR_SETTINGS: return SCR_TIMER;
  default: return SCR_TIMER;
  }
}

static Screen getPrevScreen(Screen s) {
  // Reverse of: Timer→Tasks→Calendar→Agenda→Agent→Focus→Streak→Weather→Stats→Spotify→QR→Settings
  switch (s) {
  case SCR_TIMER: return SCR_SETTINGS;
  case SCR_TASKS: return SCR_TIMER;
  case SCR_CALENDAR: return SCR_TASKS;
  case SCR_AGENDA: return SCR_CALENDAR;
  case SCR_AGENT: return SCR_AGENDA;
  case SCR_FOCUS:
#if FEATURE_LLM
    return SCR_AGENT;
#else
    return SCR_AGENDA;
#endif
  case SCR_STREAK: return SCR_FOCUS;
  case SCR_WEATHER: return SCR_STREAK;
  case SCR_STATS:
#if FEATURE_WEATHER
    return SCR_WEATHER;
#else
    return SCR_STREAK;
#endif
  case SCR_SPOTIFY: return SCR_STATS;
  case SCR_QR:
#if FEATURE_SPOTIFY
    return SCR_SPOTIFY;
#else
    return SCR_STATS;
#endif
  case SCR_SETTINGS: return SCR_QR;
  default: return SCR_TIMER;
  }
}

void navigateNext() {
  Screen next = getNextScreen(screen);
  if (next == SCR_QR || next == SCR_WIFI_INFO) {
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

void navigatePrev() {
  Screen prev = getPrevScreen(screen);
  if (prev == SCR_QR || prev == SCR_WIFI_INFO) {
    if (!portal.wifiOn) portal.begin();
  }
  if (model.st.finished && prev != SCR_TIMER) {
    alarmStop();
  }
  screen = prev;
  ui.baseDrawn = false;
  ui.settingsEditing = false;
  ui.streakEditing = false;
  ui.applyTheme();
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
    ui.drawFlashIfNeeded();
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
    int delta = step * 5;
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
    if (click == ENC_SHORT || click == ENC_LONG) {
      showingDetail = false;
      ui.drawTasks();
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
  if (click == ENC_SHORT) {
    model.taskDone[ui.tasksSel] = !model.taskDone[ui.tasksSel];
    ui.uiTick(true);
    if (model.taskDone[ui.tasksSel])
      model.streakAddCompletion();
    model.save();
    ui.drawTasks();
  }
  if (click == ENC_LONG) {
    showingDetail = true;
    ui.uiTick(true);
    ui.drawTaskDetail(ui.tasksSel);
  }
}

void handleCalendar(int step, EncClickType click) {
  if (ui.showingDayTasks) {
    if (click == ENC_SHORT || click == ENC_LONG) {
      ui.showingDayTasks = false;
      ui.baseDrawn = false;
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
  if (click == ENC_SHORT && ui.calendarSelDay > 0) {
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
    ui.drawQRFull(portal.apSSID, portal.apPass);
    ui.baseDrawn = true;
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    screen = SCR_TIMER;
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
    ui.drawWifi(portal.wifiOn, status, portal.apPass, ip);
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
  // Focus mode - blocklist management screen
  if (!ui.baseDrawn) {
    ui.drawFocus(&focusDns);
    ui.baseDrawn = true;
  }

  // Encoder scroll through domain list
  if (step != 0) {
    int count = focusDns.getBlockedCount();
    if (count > 0) {
      ui.focusDomainScroll += step;
      if (ui.focusDomainScroll < 0) ui.focusDomainScroll = 0;
      if (ui.focusDomainScroll >= count) ui.focusDomainScroll = count - 1;
      ui.drawFocusList(&focusDns);
    }
  }

  // Short click: toggle focus mode ON/OFF
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    bool newState = !focusDns.isFocusModeActive();
    focusDns.setFocusMode(newState);
    ui.baseDrawn = false;  // Redraw full screen to update status
  }

  // Long click: remove selected domain, or reset to defaults if empty
  if (click == ENC_LONG) {
    int count = focusDns.getBlockedCount();
    if (count == 0) {
      // Reset to defaults
      ui.uiTick(true);
      focusDns.loadDefaultBlockList();
      ui.focusDomainScroll = 0;
      ui.baseDrawn = false;
    } else if (ui.focusDomainScroll < count) {
      ui.uiTick(true);
      const char* domain = focusDns.getBlockedDomain(ui.focusDomainScroll);
      focusDns.removeBlockedDomain(domain);
      // Adjust scroll if needed
      if (ui.focusDomainScroll >= focusDns.getBlockedCount() && ui.focusDomainScroll > 0) {
        ui.focusDomainScroll--;
      }
      ui.baseDrawn = false;
    }
  }
}

void handleAgenda(int step, EncClickType click) {
  if (!ui.baseDrawn) {
    ui.drawAgenda();
    ui.baseDrawn = true;
  }

  // Refresh on click
  if (click == ENC_SHORT) {
    ui.uiTick(true);
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

  ledcAttach(LED_PIN, 5000, 8);
  // ledcAttach(BUZZER_PIN, 2000, 8); // Removed for Active Buzzer

  buzzerTone(1000);
  delay(50);
  buzzerOff();

#if DISPLAY_BRINGUP
  runDisplayBringup();
#endif

  Serial.println("Loading model...");
  model.begin();
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
  power.begin();
  focusDns.loadDefaultBlockList();

  screen = SCR_TIMER;
  ui.baseDrawn = false;
  ui.applyTheme();
  Serial.println("Setup complete.");
}

// ==================== MAIN LOOP ====================
void loop() {
  yield();  // Evitar watchdog y dar tiempo a tareas WiFi
  uint32_t now = millis();
  portal.loop();
  net.loop();

  // Power management deshabilitado - siempre encendido
  power.loop();

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

    // Update focus mode state in DNS server
    focusDns.setFocusMode(model.isFocusModeActive());
  }

  if (uiBeepOffAt && millis() >= uiBeepOffAt) {
    buzzerOff();
    uiBeepOffAt = 0;
  }

  ModeClickType modeClick = modeClickType();
  int step = encoderStep();
  EncClickType click = encoderClickType();

  // Register activity for power management (any input prevents sleep)
  if (step != 0 || click != ENC_NONE || modeClick != MODE_NONE) {
    power.registerActivity();
  }

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
      delay(500);
      ESP.restart();
    }
  } else {
    bothBtnDownAt = 0;
  }

  if (modeClick == MODE_LONG && !screensaverActive) {
    if (!model.st.running && !model.st.finished) {
      screenBeforeScreensaver = screen;
      screensaverActive = true;
      ui.quoteIndex = random(30);
      ui.drawQuotes();
    }
    return;
  }

  if (screensaverActive) {
    if (step != 0) {
      ui.quoteIndex = (ui.quoteIndex + step + 30) % 30;
      ui.drawQuotes();
    }
    if (modeClick != MODE_NONE || click != ENC_NONE) {
      screensaverActive = false;
      screen = screenBeforeScreensaver;
      ui.goToScreen(screen);
    }
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

  switch (screen) {
  case SCR_TIMER:
    handleTimer(step, click);
    break;
  case SCR_FOCUS:
    handleFocus(step, click);
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
}
