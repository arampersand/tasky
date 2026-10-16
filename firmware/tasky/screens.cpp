#include "screens.h"
#include "config_features.h"

const ScreenDef SCREEN_DEFS[] = {
    {"home", "Home", SCR_HOME},
#if FEATURE_AGENTS
    {"agents", "Agents", SCR_AGENTS},
#endif
#if FEATURE_BLE_HID
    {"controls", "Controls", SCR_CONTROLS},
#endif
    {"timer", "Timer", SCR_TIMER},
    {"tasks", "Tasks", SCR_TASKS},
    {"agenda", "Agenda", SCR_AGENDA},
#if FEATURE_WEATHER
    {"weather", "Weather", SCR_WEATHER},
#endif
    {"streak", "Streak", SCR_STREAK},
    {"phrases", "Phrases", SCR_PHRASES},
    {"stats", "Stats", SCR_STATS},
    {"calendar", "Calendar", SCR_CALENDAR},
    {"focus", "Focus", SCR_FOCUS},
    {"settings", "Settings", SCR_SETTINGS},
    {"qr", "Setup (QR)", SCR_QR},
#if FEATURE_LLM
    {"agent", "Agent", SCR_AGENT},
#endif
#if FEATURE_SPOTIFY
    {"music", "Music", SCR_SPOTIFY},
#endif
};
const int SCREEN_DEFS_N = sizeof(SCREEN_DEFS) / sizeof(SCREEN_DEFS[0]);

const ScreenDef *findScreen(const String &id) {
  for (int i = 0; i < SCREEN_DEFS_N; i++)
    if (id == SCREEN_DEFS[i].id)
      return &SCREEN_DEFS[i];
  return nullptr;
}

// What you use while working, then what you check, then setup. Agents and
// Controls are skipped until you use them (rebuildMenus in tasky.ino).
const char *defaultCarousel() {
  return "home,agents,controls,phrases,timer,tasks,agenda,streak,weather,settings,qr";
}
