#pragma once

#include "model.h"
#include <Arduino.h>

// Power source detection
enum PowerSource {
  PWR_UNKNOWN,
  PWR_USB,
  PWR_BATTERY
};

// Power state machine
enum PowerState {
  STATE_ALWAYS_ON,    // USB connected - full brightness, no sleep
  STATE_ACTIVE,       // Battery - normal operation
  STATE_DIM,          // Battery - dimmed display
  STATE_LIGHT_SLEEP,  // Battery - light sleep, quick wake
  STATE_DEEP_SLEEP    // Battery - deep sleep, slow wake
};

class PowerManager {
public:
  PowerManager(TaskyModel *model, int8_t displayRstPin, int8_t wakeButtonPin);

  void begin();
  void loop();

  // State queries
  PowerSource getSource() const { return source; }
  PowerState getState() const { return state; }
  bool isOnBattery() const { return source == PWR_BATTERY; }
  bool isSleeping() const { return state >= STATE_LIGHT_SLEEP; }

  // Activity tracking
  void registerActivity();  // Call on any user input
  void preventSleep(uint32_t ms);  // Prevent sleep for duration

  // Manual control
  void forceSleep();        // Immediately enter sleep
  void forceWake();         // Force wake (if in sleep)

  // Configuration
  void setDimTimeout(uint32_t ms) { dimTimeoutMs = ms; }
  void setLightSleepTimeout(uint32_t ms) { lightSleepTimeoutMs = ms; }
  void setDeepSleepTimeout(uint32_t ms) { deepSleepTimeoutMs = ms; }

  // Get battery percentage (if ADC pin configured, else returns -1)
  int getBatteryPercent() const;

private:
  TaskyModel *model;
  int8_t displayRstPin;
  int8_t wakeButtonPin;

  PowerSource source = PWR_UNKNOWN;
  PowerState state = STATE_ACTIVE;

  uint32_t lastActivity = 0;
  uint32_t sleepPreventUntil = 0;

  // Timeouts (in milliseconds)
  uint32_t dimTimeoutMs = 30000;         // 30 seconds to dim
  uint32_t lightSleepTimeoutMs = 60000;  // 1 minute to light sleep
  uint32_t deepSleepTimeoutMs = 300000;  // 5 minutes to deep sleep

  // Internal methods
  void detectPowerSource();
  void updateState();
  void enterDimMode();
  void enterLightSleep();
  void enterDeepSleep();
  void exitSleepMode();

  void dimDisplay();
  void restoreDisplay();
};
