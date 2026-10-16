#pragma once

#include "config_features.h"
#if FEATURE_AGENTS
#include <Arduino.h>

// Status of the AI coding agents on your computer (e.g. Claude Code sessions),
// pushed over USB by companion/tasky-agent.py from Claude Code hooks:
//   TASKY AGENT {"id":"<session>","label":"<project>","state":"working|waiting|done|end","msg":"..."}
//   TASKY ASK {"id":"<req>","session":"..","label":"..","tool":"Bash","detail":"npm test",
//              "options":["Allow","Deny"],"timeout":60}
//     -> later @tasky {"answer":{"id":"<req>","choice":0}}  (-1 = answer in the terminal)
//   TASKY ASK-CANCEL {"id":"<req>"}   (timed out / answered elsewhere)
enum AgentState : uint8_t { AGENT_WORKING, AGENT_WAITING, AGENT_DONE };

// A permission question from a Claude Code PermissionRequest hook
struct AgentQuestion {
  char id[16];          // empty = no question
  char tool[24];
  char detail[64];
  char options[3][16];
  uint8_t optionCount;
  uint8_t sel;
  uint32_t deadline;    // millis()
};

struct AgentSession {
  char id[40];
  char label[24];
  char msg[64];
  AgentState state;
  bool acknowledged;    // you pressed the knob on it: stop pulsing the light
  uint32_t updatedAt;   // millis()
  AgentQuestion question;
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
      if (count == MAX) {  // replace the least recently updated session...
        int o = oldest();
        if (o < 0)
          return false;    // ...but never one waiting on a question
        remove(sessions[o].id);
      }
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

  // Shows a question on the session's card (creates the card if needed).
  // Returns the session index, or -1 if the request is unusable.
  int ask(const char *reqId, const char *session, const char *label, const char *tool,
          const char *detail, const char *const options[], int optionCount, uint32_t timeoutS) {
    if (!reqId[0] || optionCount < 1)
      return -1;
    if (!update(session[0] ? session : reqId, label, "waiting", "Asks permission"))
      return -1;
    int i = find(session[0] ? session : reqId);
    AgentQuestion &q = sessions[i].question;
    memset(&q, 0, sizeof(q));
    strlcpy(q.id, reqId, sizeof(q.id));
    strlcpy(q.tool, tool, sizeof(q.tool));
    strlcpy(q.detail, detail, sizeof(q.detail));
    q.optionCount = optionCount > 3 ? 3 : optionCount;
    for (int k = 0; k < q.optionCount; k++)
      strlcpy(q.options[k], options[k], sizeof(q.options[k]));
    q.deadline = millis() + constrain(timeoutS, 5UL, 600UL) * 1000UL;
    return i;
  }

  // The question went away (answered here or in the terminal, or timed out)
  void clearQuestion(const char *reqId) {
    for (int i = 0; i < count; i++)
      if (sessions[i].question.id[0] && strcmp(sessions[i].question.id, reqId) == 0) {
        sessions[i].question.id[0] = 0;
        changed = true;
      }
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

  // Forget sessions with no news for 2 h (crashed terminals, closed laptops),
  // and questions past their deadline (the companion has given up on them).
  void expire() {
    for (int i = 0; i < count; i++)
      if (sessions[i].question.id[0] && (int32_t)(millis() - sessions[i].question.deadline) > 0)
        clearQuestion(sessions[i].question.id);
    for (int i = count - 1; i >= 0; i--)
      if (millis() - sessions[i].updatedAt > 2UL * 60UL * 60UL * 1000UL) remove(sessions[i].id);
  }

  int find(const char *id) const {
    for (int i = 0; i < count; i++)
      if (strcmp(sessions[i].id, id) == 0) return i;
    return -1;
  }
private:
  int oldest() const {  // -1 if every session has a pending question
    int o = -1;
    for (int i = 0; i < count; i++)
      if (!sessions[i].question.id[0] &&
          (o < 0 || sessions[i].updatedAt < sessions[o].updatedAt))
        o = i;
    return o;
  }
};
#endif
