#pragma once

#include "config_features.h"
#if FEATURE_BLE_HID
#include <Arduino.h>

// Bluetooth LE keyboard + media controller ("Tasky for Agents" edition).
// The XIAO C3/C6 have no native USB, so Bluetooth HID is how Tasky can press
// keys on your computer. Pair "Tasky" once in the computer's Bluetooth settings.
namespace BleHid {

// HID Consumer Control usages (16-bit)
enum Media : uint16_t {
  VOL_UP = 0xE9,
  VOL_DOWN = 0xEA,
  MUTE = 0xE2,
  PLAY_PAUSE = 0xCD,
  NEXT = 0xB5,
  PREV = 0xB6,
  BRIGHT_UP = 0x6F,
  BRIGHT_DOWN = 0x70,
  VOICE = 0xCF,  // "Voice Command" (Siri / assistant on hosts that map it)
  HOME = 0x223,  // "AC Home": a phone's home screen
};

// Keyboard modifiers
enum Mod : uint8_t { CTRL = 0x01, SHIFT = 0x02, ALT = 0x04, GUI = 0x08 };

void begin(const char *name);
bool connected();
uint64_t peer();  // identity address of the connected device (0 = none or not recognizable)
void media(uint16_t usage);              // press + release
void keys(uint8_t modifiers, uint8_t key);  // press + release (HID usage id)

}  // namespace BleHid
#endif
