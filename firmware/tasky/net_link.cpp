#include "net_link.h"
#include "model.h"
#include <WiFi.h>
#include <WiFiUdp.h>

Print *taskyOut = &Serial;
NetLink netLink;

static WiFiServer server(NetLink::PORT);
static WiFiClient client;   // the paired computer
static WiFiClient pending;  // a new connection that hasn't shown the token yet
static WiFiUDP udp;
static const size_t MAX_LINE = 2048;

void taskyBroadcast(const String &line) {
  Serial.println(line);
  netLink.send(line);
}

String NetLink::deviceId() {
  uint64_t mac = ESP.getEfuseMac();  // bytes in transmission order, low first
  char id[7];
  snprintf(id, sizeof(id), "%02x%02x%02x", (uint8_t)(mac >> 24), (uint8_t)(mac >> 32),
           (uint8_t)(mac >> 40));
  return id;
}

void NetLink::begin(TaskyModel *m, std::function<void(const String &)> h) {
  model = m;
  handler = h;
}

bool NetLink::linked() { return authed && client.connected(); }

void NetLink::send(const String &line) {
  if (linked())
    client.println(line);
}

void NetLink::loop(bool wifiUp) {
  if (!wifiUp) {
    if (started) {
      client.stop();
      pending.stop();
      server.end();
      udp.stop();
      started = authed = false;
    }
    return;
  }
  if (!started) {
    server.begin();
    udp.begin(PORT);
    started = true;
  }

  // Discovery: answer only to our own id, so a network with two Taskys works
  if (udp.parsePacket() > 0) {
    char buf[48];
    int n = udp.read(buf, sizeof(buf) - 1);
    buf[n > 0 ? n : 0] = 0;
    String want = "TASKY FIND " + deviceId();
    if (String(buf).startsWith(want)) {
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.printf("@tasky {\"id\":\"%s\",\"port\":%u}\n", deviceId().c_str(), PORT);
      udp.endPacket();
    }
  }

  // A new connection waits apart until it shows the token, so nothing on the
  // network can knock the paired computer off without it
  WiFiClient incoming = server.accept();
  if (incoming) {
    pending.stop();
    pending = incoming;
    pending.setNoDelay(true);
    acceptedAt = millis();
    pendingLine = "";
  }
  checkPending();

  if (!client)
    return;
  if (!client.connected()) {
    client.stop();
    authed = false;
    return;
  }
  while (client.available()) {
    char c = (char)client.read();
    if (c == '\r')
      continue;
    if (c != '\n') {
      if (line.length() < MAX_LINE)
        line += c;
      continue;
    }
    line.trim();
    onLine();
    line = "";
    if (!client.connected())
      return;
  }
}

// The first line of a new connection must be "TASKY AUTH <token>"; then it
// replaces the paired one (one computer at a time: the newest wins)
void NetLink::checkPending() {
  if (!pending)
    return;
  if (!pending.connected() || millis() - acceptedAt > 5000) {
    pending.stop();
    return;
  }
  while (pending.available()) {
    char c = (char)pending.read();
    if (c == '\r')
      continue;
    if (c != '\n') {
      if (pendingLine.length() < 64)
        pendingLine += c;
      continue;
    }
    pendingLine.trim();
    if (model->cfg.linkToken.length() && pendingLine == "TASKY AUTH " + model->cfg.linkToken) {
      client.stop();
      client = pending;
      pending = WiFiClient();
      authed = true;
      line = "";
      client.println("@tasky {\"ok\":true}");
    } else {
      pending.println("@tasky {\"ok\":false,\"error\":\"pair over USB first\"}");
      pending.stop();
    }
    return;
  }
}

void NetLink::onLine() {
  if (!line.startsWith("TASKY"))
    return;
  taskyOut = &client;
  handler(line);
  taskyOut = &Serial;
}
