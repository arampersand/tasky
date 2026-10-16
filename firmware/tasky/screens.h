#pragma once

#include "ui.h"
#include <Arduino.h>

// The screens you can put in the MODE carousel, with stable ids for NVS and
// the web setup ("home,timer,tasks"). Home is always first and More always
// last; everything not in the carousel is listed in More.
struct ScreenDef {
  const char *id;
  const char *label;
  Screen screen;
};

extern const ScreenDef SCREEN_DEFS[];
extern const int SCREEN_DEFS_N;

const ScreenDef *findScreen(const String &id);
const char *defaultCarousel();  // per edition
