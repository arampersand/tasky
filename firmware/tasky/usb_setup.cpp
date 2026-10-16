#include "usb_setup.h"
#include "network_manager.h"
#include "version.h"
#include <ArduinoJson.h>
#include <WiFi.h>

static const size_t MAX_LINE = 1024;

static void reply(const JsonDocument &doc) {
  Serial.print("@tasky ");
  serializeJson(doc, Serial);
  Serial.println();
}

static void replyError(const char *msg) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = msg;
  reply(doc);
}

void UsbSetup::loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r')
      continue;
    if (c == '\n') {
      line.trim();
      if (line.startsWith("TASKY"))
        handle(line);
      line = "";
    } else if (line.length() < MAX_LINE) {
      line += c;
    }
  }
}

void UsbSetup::handle(const String &cmd) {
  if (cmd == "TASKY?")
    sendStatus();
  else if (cmd == "TASKY SCAN")
    sendScan();
  else if (cmd.startsWith("TASKY SET "))
    applySet(cmd.substring(10));
  else
    replyError("unknown command");
}

void UsbSetup::sendStatus() {
  JsonDocument doc;
  doc["v"] = TASKY_VERSION;
  doc["board"] = CONFIG_IDF_TARGET;  // "esp32c3" / "esp32c6"
  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = model->cfg.wifiSSID;
  wifi["connected"] = net->isConnected();
  wifi["status"] = net->getStatusString();
  if (net->isConnected())
    wifi["ip"] = net->getIP();
  JsonObject cfg = doc["cfg"].to<JsonObject>();
  cfg["cal"] = model->cfg.calendarUrl.length() > 0;
  cfg["ghu"] = model->cfg.githubUser;
  cfg["llm"] = model->cfg.llmProvider;
  cfg["llmKey"] = model->cfg.llmApiKey.length() > 0;
  cfg["city"] = model->cfg.weatherCity;
  cfg["tz"] = model->cfg.timezone;
  JsonArray tasks = cfg["tasks"].to<JsonArray>();
  for (int i = 0; i < 3; i++)
    tasks.add(model->tasks[i]);
  reply(doc);
}

void UsbSetup::sendScan() {
  if (WiFi.getMode() == WIFI_OFF)
    WiFi.mode(WIFI_STA);  // scanning needs the station interface
  int n = WiFi.scanNetworks();  // blocking, ~2-3 s
  JsonDocument doc;
  JsonArray list = doc["scan"].to<JsonArray>();
  for (int i = 0; i < n && i < 20; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0)
      continue;
    bool dup = false;  // same network on several access points
    for (JsonObject o : list)
      if (ssid == o["s"].as<const char *>()) { dup = true; break; }
    if (dup)
      continue;
    JsonObject o = list.add<JsonObject>();
    o["s"] = ssid;
    o["r"] = WiFi.RSSI(i);
    o["o"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  reply(doc);
}

void UsbSetup::applySet(const String &json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    replyError("bad json");
    return;
  }
  bool wifiChanged = false;

  if (doc["ssid"].is<const char *>()) {
    String ssid = doc["ssid"].as<String>();
    wifiChanged = ssid != model->cfg.wifiSSID;
    model->cfg.wifiSSID = ssid;
  }
  if (doc["pass"].is<const char *>()) {
    model->cfg.wifiPass = doc["pass"].as<String>();
    wifiChanged = true;
  }
  if (doc["tz"].is<int>())
    model->cfg.timezone = (int8_t)constrain(doc["tz"].as<int>(), -12, 14);
  if (doc["cal"].is<const char *>())
    model->cfg.calendarUrl = doc["cal"].as<String>();
  if (doc["ghu"].is<const char *>())
    model->cfg.githubUser = doc["ghu"].as<String>();
  if (doc["llm"].is<int>())
    model->cfg.llmProvider = doc["llm"].as<int>() == 1 ? 1 : 0;
  if (doc["llmKey"].is<const char *>())
    model->cfg.llmApiKey = doc["llmKey"].as<String>();
  if (doc["city"].is<const char *>()) {
    String city = doc["city"].as<String>();
    if (city != model->cfg.weatherCity) {
      model->cfg.weatherLat = 0;  // re-geocode unless lat/lon come with it
      model->cfg.weatherLon = 0;
    }
    model->cfg.weatherCity = city;
  }
  if (doc["lat"].is<float>() && doc["lon"].is<float>()) {
    model->cfg.weatherLat = doc["lat"].as<float>();
    model->cfg.weatherLon = doc["lon"].as<float>();
  }
  if (doc["tasks"].is<JsonArrayConst>()) {
    JsonArrayConst tasks = doc["tasks"].as<JsonArrayConst>();
    for (int i = 0; i < 3 && i < (int)tasks.size(); i++) {
      String name = tasks[i].as<String>();
      if (name.length() == 0)
        name = "Empty";
      if (name != model->tasks[i]) {
        model->tasks[i] = name;
        model->taskDone[i] = false;
        model->taskFocusMin[i] = 0;
        if (model->currentTask == i)
          model->currentTask = -1;
      }
    }
  }

  model->save();
  if (wifiChanged) {
    net->disconnect();
    net->begin();
  }
  if (onChanged)
    onChanged();

  JsonDocument ok;
  ok["ok"] = true;
  reply(ok);
}
