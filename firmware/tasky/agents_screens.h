#pragma once
// Screen handlers for the "Tasky for Agents" edition. Included once from
// tasky.ino (needs its globals: model, ui, agents, buzzer helpers).

#if FEATURE_AGENTS
// "TASKY AGENT {json}" from companion/tasky_agent.py (Claude Code hooks)
int agentsSel = 0;
static char agentsSelId[40] = "";           // the card you're on, by id: indexes shift
bool agentsRedraw = false;                  // news arrived: repaint the card
static Screen questionReturn = SCR_AGENTS;  // where to go back after answering
static bool handleAsk(const String &cmd);
bool handleAgentCommand(const String &cmd) {
  if (cmd.startsWith("TASKY ASK"))
    return handleAsk(cmd);
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

uint32_t agentBannerUntil = 0;  // the banner covers the top of the screen until then

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
  // A banner on whatever screen you're on (Agents shows it already)
  if (waiting > lastWaiting && screen != SCR_AGENTS && screen != SCR_TOUR &&
      screen != SCR_NOTICE) {
    for (int i = 0; i < agents.count; i++) {
      const AgentSession &s = agents.sessions[i];
      if (s.state == AGENT_WAITING && !s.acknowledged) {
        ui.drawBanner((String(s.label) + " needs you").c_str());
        agentBannerUntil = millis() + 4000;
        break;
      }
    }
  }
  lastWaiting = waiting;
}

// Agents: one card per session. Turn = next card, press = seen (stops the
// light), long press = remove. A card with a question: turn = pick an option,
// press = answer, long press = leave it to the terminal.
static void selectAgent(int i) {
  agentsSel = i;
  strlcpy(agentsSelId, i >= 0 && i < agents.count ? agents.sessions[i].id : "",
          sizeof(agentsSelId));
}

// Find the selected card again after sessions came and went. False if it's
// gone (the screen must repaint before any input applies to another card).
static bool resolveSelection() {
  int i = agentsSelId[0] ? agents.find(agentsSelId) : -1;
  if (i >= 0) {
    agentsSel = i;
    return true;
  }
  bool hadOne = agentsSelId[0] != 0;
  selectAgent(agents.count ? constrain(agentsSel, 0, agents.count - 1) : 0);
  return !hadOne;
}

static int firstQuestion() {
  for (int i = 0; i < agents.count; i++)
    if (agents.sessions[i].question.id[0])
      return i;
  return -1;
}

// After the last question is gone, go back to what you were doing
static void leaveQuestions() {
  int next = firstQuestion();
  if (next >= 0) {
    selectAgent(next);
    agentsRedraw = true;
  } else if (questionReturn != SCR_AGENTS) {
    Screen back = questionReturn;
    questionReturn = SCR_AGENTS;
    if (screen == SCR_AGENTS)
      goTo(back);
  }
}

void answerQuestion(int i, int choice) {
  AgentSession &s = agents.sessions[i];
  char id[sizeof(s.question.id)];
  strlcpy(id, s.question.id, sizeof(id));
  JsonDocument d;
  d["answer"]["id"] = id;
  d["answer"]["choice"] = choice;
  Serial.print("@tasky ");
  serializeJson(d, Serial);
  Serial.println();
  if (choice >= 0) {
    snprintf(s.msg, sizeof(s.msg), "You chose: %s", s.question.options[choice]);
    s.state = AGENT_WORKING;
    model.addActivity(1);  // greener streak square
  } else {
    strlcpy(s.msg, "Answer in the terminal", sizeof(s.msg));
  }
  s.acknowledged = true;
  agents.clearQuestion(id);
  agents.changed = true;
  leaveQuestions();
}

// "TASKY ASK {json}": a Claude Code PermissionRequest hook waits on the knob.
// Jump to its card (unless the tour runs); "TASKY ASK-CANCEL" takes it back.
static bool handleAsk(const String &cmd) {
  bool cancel = cmd.startsWith("TASKY ASK-CANCEL ");
  if (!cancel && !cmd.startsWith("TASKY ASK "))
    return false;
  JsonDocument d;
  if (deserializeJson(d, cmd.substring(cancel ? 17 : 10))) {
    Serial.println("@tasky {\"ok\":false,\"error\":\"bad json\"}");
    return true;
  }
  const char *id = d["id"] | "";
  if (cancel) {
    agents.clearQuestion(id);
    Serial.println("@tasky {\"ok\":true}");
    leaveQuestions();
    return true;
  }
  const char *opts[3];
  int n = 0;
  for (JsonVariantConst o : d["options"].as<JsonArrayConst>())
    if (n < 3)
      opts[n++] = o | "";
  int i = agents.ask(id, d["session"] | "", d["label"] | "", d["tool"] | "Tool",
                     d["detail"] | "", opts, n, d["timeout"] | 60);
  if (i < 0) {
    Serial.println("@tasky {\"ok\":false,\"error\":\"need id and options\"}");
    return true;
  }
  Serial.println("@tasky {\"ok\":true}");
  if (screen == SCR_TOUR)
    return true;
  resolveSelection();
  bool showingQuestion = screen == SCR_AGENTS && agentsSel < agents.count &&
                         agents.sessions[agentsSel].question.id[0] &&
                         agentsSel != i;
  if (showingQuestion)
    return true;  // you're answering another one; this card waits its turn
  selectAgent(i);
  if (screen == SCR_AGENTS) {
    agentsRedraw = true;
  } else {
    if (screen == SCR_NOTICE)
      buzzerOff();  // a meeting alert may be mid-beep
    questionReturn = (screen == SCR_AMBIENT || screen == SCR_NOTICE) ? screenBeforeOverlay : screen;
    goTo(SCR_AGENTS);
  }
  return true;
}

void handleAgents(int step, EncClickType click) {
  static uint32_t lastTickAt = 0;
  static int frame = 0;
  if (!resolveSelection())
    agentsRedraw = true;  // your card is gone: show where you are first
  if (agentsRedraw && (step != 0 || click != ENC_NONE)) {
    // The card changed under your finger: show it and drop this input, which
    // was meant for what you saw before (never answer the wrong question).
    agentsRedraw = false;
    ui.drawAgents(agents, agentsSel);
    return;
  }
  if (!ui.baseDrawn || agentsRedraw) {
    agentsRedraw = false;
    ui.drawAgents(agents, agentsSel);
  }
  if (agents.count == 0)
    return;
  AgentSession &s = agents.sessions[agentsSel];
  AgentQuestion &q = s.question;

  if (q.id[0]) {
    if (step != 0) {
      q.sel = ((q.sel + step) % q.optionCount + q.optionCount) % q.optionCount;
      ui.uiTick(false);
      ui.drawQuestionOptions(q);
    }
    if (click == ENC_SHORT || click == ENC_LONG) {
      ui.uiTick(true);
      answerQuestion(agentsSel, click == ENC_SHORT ? q.sel : -1);
      return;
    }
  } else {
    if (step != 0) {
      // knob up = next card on the right (the knob's steps count the other way)
      selectAgent(((agentsSel - step) % agents.count + agents.count) % agents.count);
      ui.uiTick(false);
      ui.drawAgents(agents, agentsSel);
      return;
    }
    if (click == ENC_SHORT) {
      ui.uiTick(true);
      s.acknowledged = true;
      agents.changed = true;
    }
    if (click == ENC_LONG) {
      ui.uiTick(true);
      agents.remove(s.id);
      return;
    }
  }

  // The robot moves twice a second; a question counts down instead
  if (millis() - lastTickAt >= 500) {
    lastTickAt = millis();
    frame++;
    if (q.id[0])
      ui.drawQuestionCountdown(q);
    else
      ui.drawAgentSprite(s, frame);
  }
}
#endif

#if FEATURE_BLE_HID
// Controls: a row of computer keys like the Mac's top row. Turn = pick a key,
// press = send it. Volume and Brightness are dials: press to grab, turn to
// adjust, press to let go. Long press = switch Mac / Windows shortcuts.
struct ComputerKey {
  const char *mac, *win;   // names
  uint16_t media;          // consumer usage (both systems), 0 = keyboard shortcut
  uint8_t macMods, macKey; // HID keyboard usage ids
  uint8_t winMods, winKey;
  uint16_t up, down;       // dial: consumer usages for each knob direction
};
using BleHid::CTRL;
using BleHid::SHIFT;
using BleHid::GUI;
static const ComputerKey KEYS[] = {
    {"Volume", "Volume", 0, 0, 0, 0, 0, BleHid::VOL_UP, BleHid::VOL_DOWN},
    {"Mute", "Mute", BleHid::MUTE, 0, 0, 0, 0, 0, 0},
    {"Play / Pause", "Play / Pause", BleHid::PLAY_PAUSE, 0, 0, 0, 0, 0, 0},
    {"Next", "Next", BleHid::NEXT, 0, 0, 0, 0, 0, 0},
    {"Previous", "Previous", BleHid::PREV, 0, 0, 0, 0, 0, 0},
    {"Brightness", "Brightness", 0, 0, 0, 0, 0, BleHid::BRIGHT_UP, BleHid::BRIGHT_DOWN},
    {"Mission Control", "Task View", 0, CTRL, 0x52, GUI, 0x2B, 0, 0},     // Ctrl+Up / Win+Tab
    {"Spotlight", "Search", 0, GUI, 0x2C, GUI, 0x16, 0, 0},              // Cmd+Space / Win+S
    {"Dictation", "Dictation", 0, 0, 0, GUI, 0x0B, 0, 0},                // Voice key / Win+H
    {"Desktop left", "Desktop left", 0, CTRL, 0x50, CTRL | GUI, 0x50, 0, 0},
    {"Desktop right", "Desktop right", 0, CTRL, 0x4F, CTRL | GUI, 0x4F, 0, 0},
    {"Screenshot", "Screenshot", 0, GUI | SHIFT, 0x21, GUI | SHIFT, 0x16, 0, 0},  // Cmd+Shift+4 / Win+Shift+S
    {"Lock screen", "Lock screen", 0, CTRL | GUI, 0x14, GUI, 0x0F, 0, 0},  // Ctrl+Cmd+Q / Win+L
};
static const int KEYS_N = sizeof(KEYS) / sizeof(KEYS[0]);
static const int KEY_DICTATION = 8;
int keySel = 0;
bool dialGrabbed = false;

static const char *keyName(int i) {
  i = (i + KEYS_N) % KEYS_N;
  return model.cfg.hostOs ? KEYS[i].win : KEYS[i].mac;
}

void drawControlsScreen() {
  const ComputerKey &k = KEYS[keySel];
  const char *hint = dialGrabbed  ? "Turn: adjust  Press: done"
                     : k.up       ? "Press: grab the dial"
                                  : "Press: send";
  ui.drawControls(keyName(keySel - 1), keyName(keySel), keyName(keySel + 1), hint,
                  dialGrabbed, BleHid::connected(), model.cfg.hostOs == 1);
}

static void sendKey(const ComputerKey &k) {
  if (k.media) {
    BleHid::media(k.media);
  } else if (&k == &KEYS[KEY_DICTATION] && !model.cfg.hostOs) {
    BleHid::media(BleHid::VOICE);  // macOS maps the consumer Voice key
  } else if (model.cfg.hostOs) {
    BleHid::keys(k.winMods, k.winKey);
  } else {
    BleHid::keys(k.macMods, k.macKey);
  }
}

void handleControls(int step, EncClickType click) {
  static bool wasConnected = false;
  if (BleHid::connected() != wasConnected) {
    wasConnected = BleHid::connected();
    ui.baseDrawn = false;
  }
  if (!ui.baseDrawn)
    drawControlsScreen();
  const ComputerKey &k = KEYS[keySel];
  if (click == ENC_LONG) {
    ui.uiTick(true);
    model.cfg.hostOs = model.cfg.hostOs ? 0 : 1;
    model.save();
    dialGrabbed = false;
    ui.baseDrawn = false;
    return;
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    if (k.up)
      dialGrabbed = !dialGrabbed;
    else
      sendKey(k);
    drawControlsScreen();
  }
  if (step == 0)
    return;
  if (dialGrabbed) {  // clockwise = up (the knob's steps count the other way, as on the Timer)
    for (int i = 0; i < abs(step); i++)
      BleHid::media(step < 0 ? k.up : k.down);
    return;
  }
  keySel = ((keySel + step) % KEYS_N + KEYS_N) % KEYS_N;
  ui.uiTick(false);
  drawControlsScreen();
}
#endif
