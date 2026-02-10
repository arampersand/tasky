#include "power_manager.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

PowerManager::PowerManager(TaskyModel *m, int8_t rstPin, int8_t wakePin)
    : model(m), displayRstPin(rstPin), wakeButtonPin(wakePin) {}

void PowerManager::begin() {
  detectPowerSource();
  lastActivity = millis();

  // Configure wake pin for sleep modes
  if (wakeButtonPin >= 0) {
    gpio_set_direction((gpio_num_t)wakeButtonPin, GPIO_MODE_INPUT);
    gpio_pullup_en((gpio_num_t)wakeButtonPin);
  }

  Serial.printf("[Power] Source: %s\n",
                source == PWR_USB ? "USB" : "Battery");
}

void PowerManager::loop() {
  // Power management DISABLED - siempre encendido, nunca dormir
  // Evita apagado accidental y problemas de estabilidad
  state = STATE_ALWAYS_ON;
  lastActivity = millis();
}

void PowerManager::detectPowerSource() {
  // No VBUS detection hardware - assume USB when connected (safe default).
  // For battery detection, add a voltage divider on 5V to an ADC pin.
  static bool sourceDetected = false;
  if (sourceDetected) return;

  // Always default to USB to prevent sleep when programming/debugging.
  // Without hardware detection, we cannot reliably distinguish USB vs battery.
  source = PWR_USB;
  sourceDetected = true;
  Serial.println("[Power] Defaulting to USB mode (no VBUS detection available)");
  Serial.println("[Power] Note: Add voltage divider to ADC for battery detection");
}

void PowerManager::updateState() {
  uint32_t now = millis();
  uint32_t inactive = now - lastActivity;

  // Check if sleep is being prevented
  if (sleepPreventUntil > 0 && now < sleepPreventUntil) {
    if (state != STATE_ACTIVE) {
      state = STATE_ACTIVE;
      restoreDisplay();
    }
    return;
  }

  // Check if timer is running - don't sleep during active timer
  if (model->st.running) {
    if (state != STATE_ACTIVE) {
      state = STATE_ACTIVE;
      restoreDisplay();
    }
    lastActivity = now;  // Keep activity fresh during timer
    return;
  }

  // State machine based on inactivity
  if (inactive >= deepSleepTimeoutMs && state != STATE_DEEP_SLEEP) {
    enterDeepSleep();
  } else if (inactive >= lightSleepTimeoutMs && state < STATE_LIGHT_SLEEP) {
    enterLightSleep();
  } else if (inactive >= dimTimeoutMs && state < STATE_DIM) {
    enterDimMode();
  }
}

void PowerManager::registerActivity() {
  lastActivity = millis();

  // If we were in dim or about to sleep, restore
  if (state == STATE_DIM) {
    state = STATE_ACTIVE;
    restoreDisplay();
  }
}

void PowerManager::preventSleep(uint32_t ms) {
  uint32_t now = millis();
  uint32_t newUntil = now + ms;
  if (newUntil > sleepPreventUntil) {
    sleepPreventUntil = newUntil;
  }
}

void PowerManager::forceSleep() {
  if (source == PWR_USB) {
    Serial.println("[Power] Cannot sleep on USB power");
    return;
  }
  enterLightSleep();
}

void PowerManager::forceWake() {
  exitSleepMode();
}

int PowerManager::getBatteryPercent() const {
  // Without a dedicated battery monitoring circuit, we can't know battery level
  // Return -1 to indicate unknown
  return -1;
}

void PowerManager::enterDimMode() {
  state = STATE_DIM;
  dimDisplay();
  Serial.println("[Power] Entering dim mode");
}

void PowerManager::enterLightSleep() {
  state = STATE_LIGHT_SLEEP;
  Serial.println("[Power] Entering light sleep...");
  Serial.flush();

  // Dim display before sleep
  dimDisplay();

  // Configure wake sources
  if (wakeButtonPin >= 0) {
    esp_sleep_enable_gpio_wakeup();
    gpio_wakeup_enable((gpio_num_t)wakeButtonPin, GPIO_INTR_LOW_LEVEL);
  }

  // If timer has remaining time, set timer wakeup
  if (model->st.running) {
    uint64_t remainUs = (uint64_t)model->remainingMs() * 1000ULL;
    esp_sleep_enable_timer_wakeup(remainUs);
  }

  // Enter light sleep
  esp_light_sleep_start();

  // --- Woke up ---
  Serial.println("[Power] Woke from light sleep");
  exitSleepMode();
}

void PowerManager::enterDeepSleep() {
  state = STATE_DEEP_SLEEP;
  Serial.println("[Power] Entering deep sleep...");
  Serial.flush();

  // Save any pending state
  model->save();

  // Turn off display completely
  if (displayRstPin >= 0) {
    digitalWrite(displayRstPin, LOW);
  }

  // Configure wake source - GPIO wake for deep sleep on ESP32-C3/C6
  if (wakeButtonPin >= 0) {
    // ESP32-C3/C6 uses gpio_deep_sleep_wakeup
    uint64_t gpioMask = 1ULL << wakeButtonPin;
    esp_deep_sleep_enable_gpio_wakeup(gpioMask, ESP_GPIO_WAKEUP_GPIO_LOW);
  }

  // Enter deep sleep (does not return - will restart on wake)
  esp_deep_sleep_start();
}

void PowerManager::exitSleepMode() {
  state = STATE_ACTIVE;
  lastActivity = millis();
  restoreDisplay();
}

void PowerManager::dimDisplay() {
  // Reduce display brightness to minimum
  // This is done through the UI's brightness setting
  // For now, just set a flag - UI will handle actual dimming
  if (model->cfg.screenBright > 30) {
    // Store original brightness if needed
    // model->cfg.screenBright will be restored on wake
  }
}

void PowerManager::restoreDisplay() {
  // Restore full brightness
  // If display was reset, it needs to be re-initialized
  if (displayRstPin >= 0) {
    // Pulse reset high if it was low
    digitalWrite(displayRstPin, HIGH);
  }
}
