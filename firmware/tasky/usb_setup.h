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
//   TASKY SET {json}  -> @tasky {"ok":true}   keys: ssid pass tz cal ghu llm
//                                                   llmKey city lat lon tasks
//
// Replies start with "@tasky " so the page can ignore regular debug logs.
// Secrets are write-only: TASKY? only reports whether they are set.
class UsbSetup {
public:
  UsbSetup(TaskyModel *model, TaskyNetManager *net) : model(model), net(net) {}

  void loop();
  // Called after a successful SET (e.g. to refresh weather/calendar).
  std::function<void()> onChanged;

private:
  TaskyModel *model;
  TaskyNetManager *net;
  String line;

  void handle(const String &cmd);
  void sendStatus();
  void sendScan();
  void applySet(const String &json);
};
