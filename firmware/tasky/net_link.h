#pragma once
// The computer companion over Wi-Fi: the same line protocol as USB
// ("TASKY?", "TASKY AGENT {..}", "TASKY ASK {..}"), so Claude Code's cards and
// approvals work with Tasky on battery, anywhere on your network.
//
//   TCP 7778: first line "TASKY AUTH <token>" -> @tasky {"ok":true}, then
//             commands as over USB. One computer at a time (a new one
//             replaces the old). The token comes from TASKY? over USB, so
//             pairing is plugging Tasky in once.
//   UDP 7778: "TASKY FIND <id>" -> @tasky {"id":..,"port":7778} from Tasky's
//             address, so the companion finds Tasky when its IP changes.
#include <Arduino.h>
#include <functional>

class TaskyModel;

// Where command replies go: Serial, or the Wi-Fi companion while it's asking
extern Print *taskyOut;
// Lines nobody asked for (a knob answer): to USB and the Wi-Fi companion
void taskyBroadcast(const String &line);

class NetLink {
public:
  static constexpr uint16_t PORT = 7778;
  void begin(TaskyModel *model, std::function<void(const String &cmd)> handler);
  void loop(bool wifiUp);
  void send(const String &line);  // to the paired companion, if any
  bool linked();                  // a companion is connected and paired
  static String deviceId();       // "3f7757": the last 3 bytes of the MAC

private:
  TaskyModel *model = nullptr;
  std::function<void(const String &)> handler;
  bool started = false;
  bool authed = false;
  uint32_t acceptedAt = 0;  // when the pending connection arrived
  String line, pendingLine;
  void onLine();
  void checkPending();
};

extern NetLink netLink;
