#pragma once

#include "model.h"
#include <Arduino.h>
#include <functional>

class TaskyNetManager;

// Line-based setup protocol over the USB serial port, used by the web setup
// wizard (tasky-flash.vercel.app) right after flashing. Physical USB access is
// the trust boundary, so no extra auth.
//
//   TASKY?            -> @tasky {"v":..,"board":..,"wifi":{..},"cfg":{..}}
//   TASKY SCAN        -> @tasky {"scan":[{"s":ssid,"r":rssi,"o":open},..]}
//   TASKY SET {json}  -> @tasky {"ok":true}   keys: ssid pass tz time cal ghu
//                                                   llm llmKey city lat lon tasks
//                                                   light (0 = LED, 1 = RGB bar)
//                                                   screens ["timer",..] phrases [".."]
//   TASKY CALTEST     -> @tasky {"ok":true,"events":5,"today":2,"next":"..","nextTime":"10:00"}
//                        or {"ok":false,"error":".."}  (handled in tasky.ino; needs Wi-Fi)
//
// Replies start with "@tasky " so the page can ignore regular debug logs.
// Secrets are write-only: TASKY? only reports whether they are set.
class UsbSetup {
public:
  UsbSetup(TaskyModel *model, TaskyNetManager *net) : model(model), net(net) {}

  void loop();
  void handle(const String &cmd);  // one "TASKY ..." line (USB or Wi-Fi)
  // Called after a successful SET (e.g. to refresh weather/calendar).
  std::function<void()> onChanged;
  // Edition-specific commands ("TASKY AGENT {...}"): return true if handled.
  std::function<bool(const String &cmd)> onCommand;
  // Set when a SET carried "light"; onChanged clears it after re-init.
  bool lightSet = false;
  uint32_t lastCommandAt = 0;  // a computer talked to Tasky (the tour moves on)

private:
  TaskyModel *model;
  TaskyNetManager *net;
  String line;

  void sendStatus();
  void sendScan();
  void applySet(const String &json);
};
