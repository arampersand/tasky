#include "wifi_portal.h"
#include "network_manager.h" // [NEW] Needed for full definition

WiFiPortal::WiFiPortal(TaskyModel *m, TaskyNetManager *n)
    : server(80), model(m), net(n) {}

void WiFiPortal::begin() {
  if (wifiOn)
    return;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID.c_str(), apPass.c_str());

  IPAddress ip = WiFi.softAPIP();

  // DNS hijack
  dnsServer.start(53, "*", ip);

  // Routes
  server.on("/", HTTP_GET, [this]() { handleRoot(); });
  server.on("/save", HTTP_POST, [this]() { handleSave(); });

  // Captive portal handlers
  auto redirect = [this]() {
    server.sendHeader(
        "Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  };

  server.on("/generate_204", HTTP_GET, redirect);
  server.on("/gen_204", HTTP_GET, redirect);
  server.on("/connecttest.txt", HTTP_GET, redirect);
  server.on("/redirect", HTTP_GET, redirect);
  server.on("/canonical.html", HTTP_GET, redirect);
  server.on("/hotspot-detect.html", HTTP_GET,
            [this]() { server.send(200, "text/html", htmlPage()); });
  server.on("/ncsi.txt", HTTP_GET,
            [this]() { server.send(200, "text/plain", "Microsoft NCSI"); });
  server.onNotFound(redirect);
  server.on("/ping", HTTP_GET,
            [this]() { server.send(200, "text/plain", "pong"); });

  server.begin();
  wifiOn = true;
}

void WiFiPortal::stop() {
  if (!wifiOn)
    return;
  dnsServer.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiOn = false;
}

void WiFiPortal::loop() {
  if (wifiOn) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
}

String WiFiPortal::htmlPage() {
  String s;
  s += "<!doctype html><html><head>";
  s += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<style>body{font-family:Arial;background:#111;color:#fff;padding:16px}";
  s += "input,button{width:100%;padding:12px;margin:8px 0;font-size:16px}";
  s += "button{background:#00e5cc;border:none;color:#000;font-weight:bold}</"
       "style>";
  s += "</head><body>";
  s += "<h2>Tasky Setup</h2>";
  s += "<form method='POST' action='/save'>";
  for (int i = 0; i < 3; i++) {
    s += "<label>Task " + String(i + 1) + "</label>";
    s += "<input name='t" + String(i) + "' value='" + model->tasks[i] +
         "' maxlength='40'>";
  }
  s += "<label>Pomodoro Minutes</label>";
  s += "<input name='work' value='" + String(model->cfg.workMin) + "'>";
  s += "<hr><label>Home WiFi SSID</label>";
  s += "<input name='ssid' value='" + model->cfg.wifiSSID + "' maxlength='32'>";
  s += "<label>Home WiFi Password</label>";
  s += "<input name='pass' type='password' value='" + model->cfg.wifiPass +
       "' maxlength='64'>";
  s += "<label>Timezone (UTC Offset)</label>";
  s += "<input name='tz' type='number' value='" + String(model->cfg.timezone) +
       "'>";
  s += "<hr><h3>Integrations</h3>";
  s += "<label>GitHub Username</label>";
  s += "<input name='ghu' value='" + model->cfg.githubUser + "'>";
  s += "<label>Calendar URL (iCal)</label>";
  s += "<input name='cal' value='" + model->cfg.calendarUrl + "'>";
  s += "<button type='submit'>Save & Connect</button>";
  s += "</form></body></html>";
  return s;
}

void WiFiPortal::handleRoot() { server.send(200, "text/html", htmlPage()); }

void WiFiPortal::handleSave() {
  for (int i = 0; i < 3; i++) {
    String k = "t" + String(i);
    if (server.hasArg(k)) {
      model->tasks[i] = server.arg(k);
      if (model->tasks[i].length() == 0)
        model->tasks[i] = "Empty";
      model->taskDone[i] = false;
    }
  }

  if (server.hasArg("work")) {
    int v = server.arg("work").toInt();
    if (v < 5)
      v = 5;
    if (v > 90)
      v = 90;
    model->cfg.workMin = (uint8_t)v;
  }

  if (server.hasArg("ssid"))
    model->cfg.wifiSSID = server.arg("ssid");
  if (server.hasArg("pass"))
    model->cfg.wifiPass = server.arg("pass");
  if (server.hasArg("tz"))
    model->cfg.timezone = (int8_t)server.arg("tz").toInt();

  if (server.hasArg("ghu"))
    model->cfg.githubUser = server.arg("ghu");
  if (server.hasArg("cal"))
    model->cfg.calendarUrl = server.arg("cal");

  model->save();

  // [NEW] Trigger reconnection
  if (net) {
    net->disconnect(); // Clear old state
    net->begin();      // Start new connection attempt
  }

  server.send(200, "text/html",
              "<h3>Saved & Verify...</h3><p>Check device screen for connection "
              "status.</p><p><a href='/'>Back</a></p>");
}
