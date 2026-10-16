#pragma once
// Screen handlers for the "Tasky for Agents" edition. Included once from
// tasky.ino (needs its globals: model, ui, agents, buzzer helpers).

#if FEATURE_AGENTS
// "TASKY AGENT {json}" from companion/tasky-agent.py (Claude Code hooks)
bool handleAgentCommand(const String &cmd) {
  if (!cmd.startsWith("TASKY AGENT "))
    return false;
  JsonDocument d;
  if (deserializeJson(d, cmd.substring(12))) {
    Serial.println("@tasky {\"ok\":false,\"error\":\"bad json\"}");
    return true;
  }
  const char *id = d["id"] | "";
  bool ok = id[0] && agents.update(id, d["label"] | "", d["state"] | "", d["msg"] | "");
  Serial.println(ok ? "@tasky {\"ok\":true}"
                    : "@tasky {\"ok\":false,\"error\":\"need id and a valid state\"}");
  return true;
}

// Home status line + a short beep when a new agent starts waiting
void refreshAgentStatus() {
  static int lastWaiting = 0;
  int waiting = agents.waitingCount(true);
  int working = agents.workingCount();
  if (waiting > 0) {
    ui.statusLine = String(waiting) + (waiting == 1 ? " agent needs you" : " agents need you");
    ui.statusColor = ui.C_ORANGE;
  } else if (working > 0) {
    ui.statusLine = String(working) + (working == 1 ? " agent working" : " agents working");
    ui.statusColor = ui.C_TEAL;
  } else {
    ui.statusLine = "";
  }
  if (waiting > lastWaiting && model.cfg.soundOn && !model.isFocusModeActive()) {
    buzzerTone(2400);
    uiBeepOffAt = millis() + 90;
  }
  lastWaiting = waiting;
}

// Agents: turn = select, press = acknowledge (stop the light), long = dismiss
int agentsSel = 0;
void handleAgents(int step, EncClickType click) {
  agentsSel = agents.count ? constrain(agentsSel, 0, agents.count - 1) : 0;
  if (!ui.baseDrawn) {
    ui.drawAgents(agents, agentsSel);
    ui.baseDrawn = true;
  }
  if (agents.count == 0)
    return;
  if (step != 0) {
    agentsSel = (agentsSel + step + agents.count * 4) % agents.count;
    ui.uiTick(false);
    ui.baseDrawn = false;
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    agents.sessions[agentsSel].acknowledged = true;
    agents.changed = true;
  }
  if (click == ENC_LONG) {
    ui.uiTick(true);
    agents.remove(agents.sessions[agentsSel].id);
  }
}
#endif

#if FEATURE_BLE_HID
// Controls: the knob becomes a volume / brightness / media / shortcut dial.
struct Shortcut {
  const char *name;
  uint8_t mods;
  uint8_t key;     // HID keyboard usage, 0 = send `media` instead
  uint16_t media;
};
// macOS shortcuts; edit to taste (HID usage ids: https://usb.org/hid)
static const Shortcut SHORTCUTS[] = {
    {"Spotlight", BleHid::GUI, 0x2C, 0},                   // Cmd+Space
    {"Voice / Siri", 0, 0, BleHid::VOICE},
    {"Screenshot", BleHid::GUI | BleHid::SHIFT, 0x21, 0},  // Cmd+Shift+4
    {"Mission Control", BleHid::CTRL, 0x52, 0},            // Ctrl+Up
    {"Lock screen", BleHid::CTRL | BleHid::GUI, 0x14, 0},  // Ctrl+Cmd+Q
};
static const int SHORTCUTS_N = sizeof(SHORTCUTS) / sizeof(SHORTCUTS[0]);
enum ControlMode { CTL_VOLUME, CTL_BRIGHTNESS, CTL_MEDIA, CTL_SHORTCUTS, CTL_COUNT };
int controlMode = CTL_VOLUME;
int shortcutSel = 0;

void drawControlsScreen() {
  static const char *MODE[] = {"Volume", "Brightness", "Media", "Shortcuts"};
  static const char *HINT[] = {"Turn: volume  Press: mute", "Turn: screen brightness",
                               "Turn: next/prev  Press: play", "Turn: pick  Press: run"};
  const char *value = controlMode == CTL_SHORTCUTS ? SHORTCUTS[shortcutSel].name : "";
  ui.drawControls(MODE[controlMode], value, HINT[controlMode], BleHid::connected());
}

// Long press = next mode
void handleControls(int step, EncClickType click) {
  static bool wasConnected = false;
  if (!ui.baseDrawn || BleHid::connected() != wasConnected) {
    wasConnected = BleHid::connected();
    drawControlsScreen();
    ui.baseDrawn = true;
  }
  if (click == ENC_LONG) {
    ui.uiTick(true);
    controlMode = (controlMode + 1) % CTL_COUNT;
    ui.baseDrawn = false;
    return;
  }
  for (int i = 0; i < abs(step); i++) {
    bool up = step > 0;
    switch (controlMode) {
    case CTL_VOLUME: BleHid::media(up ? BleHid::VOL_UP : BleHid::VOL_DOWN); break;
    case CTL_BRIGHTNESS: BleHid::media(up ? BleHid::BRIGHT_UP : BleHid::BRIGHT_DOWN); break;
    case CTL_MEDIA: BleHid::media(up ? BleHid::NEXT : BleHid::PREV); break;
    case CTL_SHORTCUTS:
      shortcutSel = (shortcutSel + (up ? 1 : SHORTCUTS_N - 1)) % SHORTCUTS_N;
      ui.baseDrawn = false;
      break;
    }
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    if (controlMode == CTL_VOLUME)
      BleHid::media(BleHid::MUTE);
    else if (controlMode == CTL_MEDIA)
      BleHid::media(BleHid::PLAY_PAUSE);
    else if (controlMode == CTL_SHORTCUTS) {
      const Shortcut &sc = SHORTCUTS[shortcutSel];
      if (sc.media)
        BleHid::media(sc.media);
      else
        BleHid::keys(sc.mods, sc.key);
    }
  }
}
#endif
