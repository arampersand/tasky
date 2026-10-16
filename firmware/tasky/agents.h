#pragma once

#include "config_features.h"
#if FEATURE_AGENTS
#include <Arduino.h>

// Status of the AI coding agents on your computer (e.g. Claude Code sessions),
// pushed over USB by companion/tasky-agent.py from Claude Code hooks:
//   TASKY AGENT {"id":"<session>","label":"<project>","state":"working|waiting|done|end","msg":"..."}
enum AgentState : uint8_t { AGENT_WORKING, AGENT_WAITING, AGENT_DONE };

struct AgentSession {
  char id[40];
  char label[24];
  char msg[64];
  AgentState state;
  bool acknowledged;    // you pressed the knob on it: stop pulsing the light
  uint32_t updatedAt;   // millis()
};

class AgentBoard {
public:
  static const int MAX = 4;
  AgentSession sessions[MAX];
  int count = 0;
  bool changed = false;  // UI should redraw

  // Returns false for an unknown state string.
  bool update(const char *id, const char *label, const char *state, const char *msg) {
    if (strcmp(state, "end") == 0) {
      remove(id);
      return true;
    }
    AgentState st;
    if (strcmp(state, "working") == 0) st = AGENT_WORKING;
    else if (strcmp(state, "waiting") == 0) st = AGENT_WAITING;
    else if (strcmp(state, "done") == 0) st = AGENT_DONE;
    else return false;

    int i = find(id);
    bool isNew = i < 0;
    if (isNew) {
      if (count == MAX)  // replace the least recently updated session
        remove(sessions[oldest()].id);
      i = count++;
      memset(&sessions[i], 0, sizeof(AgentSession));
      strlcpy(sessions[i].id, id, sizeof(sessions[i].id));
    }
    AgentSession &s = sessions[i];
    if (label && *label) strlcpy(s.label, label, sizeof(s.label));
    else if (!s.label[0]) strlcpy(s.label, id, sizeof(s.label));
    strlcpy(s.msg, msg ? msg : "", sizeof(s.msg));
    if (isNew || st != s.state) s.acknowledged = false;
    s.state = st;
    s.updatedAt = millis();
    changed = true;
    return true;
  }

  void remove(const char *id) {
    int i = find(id);
    if (i < 0) return;
    for (int j = i; j < count - 1; j++) sessions[j] = sessions[j + 1];
    count--;
    changed = true;
  }

  int waitingCount(bool onlyUnacknowledged = false) const {
    int n = 0;
    for (int i = 0; i < count; i++)
      if (sessions[i].state == AGENT_WAITING && !(onlyUnacknowledged && sessions[i].acknowledged)) n++;
    return n;
  }

  int workingCount() const {
    int n = 0;
    for (int i = 0; i < count; i++)
      if (sessions[i].state == AGENT_WORKING) n++;
    return n;
  }

  // Forget sessions with no news for 2 h (crashed terminals, closed laptops).
  void expire() {
    for (int i = count - 1; i >= 0; i--)
      if (millis() - sessions[i].updatedAt > 2UL * 60UL * 60UL * 1000UL) remove(sessions[i].id);
  }

private:
  int find(const char *id) const {
    for (int i = 0; i < count; i++)
      if (strcmp(sessions[i].id, id) == 0) return i;
    return -1;
  }
  int oldest() const {
    int o = 0;
    for (int i = 1; i < count; i++)
      if (sessions[i].updatedAt < sessions[o].updatedAt) o = i;
    return o;
  }
};
#endif
