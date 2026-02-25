#include "focus_dns.h"

FocusDNS::FocusDNS(TaskyModel* m) : model(m) {
  focusModeEnabled = false;
  serverRunning = false;
  blockedCount = 0;
  blockedQueries = 0;
  totalQueries = 0;
}

void FocusDNS::begin(IPAddress apIP) {
  captiveIP = apIP;

  if (dnsUdp.begin(DNS_PORT)) {
    serverRunning = true;
    Serial.println("[FocusDNS] Started on port 53");
  } else {
    Serial.println("[FocusDNS] Failed to start");
  }
}

void FocusDNS::stop() {
  if (serverRunning) {
    dnsUdp.stop();
    serverRunning = false;
    Serial.println("[FocusDNS] Stopped");
  }
}

void FocusDNS::loop() {
  if (!serverRunning) return;

  int packetSize = dnsUdp.parsePacket();
  if (packetSize == 0) return;

  totalQueries++;

  // Read DNS query
  uint8_t buffer[512];
  int len = dnsUdp.read(buffer, sizeof(buffer));
  if (len < 12) return;  // Invalid DNS packet

  // Store remote info before parsing
  IPAddress remoteIP = dnsUdp.remoteIP();
  uint16_t remotePort = dnsUdp.remotePort();

  // Parse domain name from query
  String domain = parseDomainFromQuery(buffer, len);

  // Check if should block (only when focus mode active)
  if (focusModeEnabled && shouldBlockDomain(domain.c_str())) {
    blockedQueries++;
    sendBlockedResponse(buffer, len, remoteIP, remotePort);
    Serial.printf("[FocusDNS] BLOCKED: %s\n", domain.c_str());
  } else {
    // Redirect to captive portal (standard behavior)
    sendCaptiveResponse(buffer, len, remoteIP, remotePort);
  }
}

void FocusDNS::setFocusMode(bool enabled) {
  if (focusModeEnabled != enabled) {
    focusModeEnabled = enabled;
    Serial.printf("[FocusDNS] Focus mode: %s\n", enabled ? "ON" : "OFF");
  }
}

bool FocusDNS::isFocusModeActive() const {
  return focusModeEnabled;
}

void FocusDNS::loadDefaultBlockList() {
  clearBlockList();

  // Social Media - Core domains only
  addBlockedDomain("facebook.com");
  addBlockedDomain("instagram.com");
  addBlockedDomain("twitter.com");
  addBlockedDomain("x.com");
  addBlockedDomain("tiktok.com");
  addBlockedDomain("youtube.com");
  addBlockedDomain("reddit.com");
  addBlockedDomain("snapchat.com");

  Serial.printf("[FocusDNS] Loaded %d blocked domains\n", blockedCount);
}

void FocusDNS::addBlockedDomain(const char* domain) {
  if (blockedCount >= MAX_BLOCKED_DOMAINS) return;

  strncpy(blockedDomains[blockedCount], domain, MAX_DOMAIN_LENGTH - 1);
  blockedDomains[blockedCount][MAX_DOMAIN_LENGTH - 1] = '\0';
  blockedCount++;
}

void FocusDNS::removeBlockedDomain(const char* domain) {
  for (int i = 0; i < blockedCount; i++) {
    if (strcmp(blockedDomains[i], domain) == 0) {
      // Shift remaining domains
      for (int j = i; j < blockedCount - 1; j++) {
        strcpy(blockedDomains[j], blockedDomains[j + 1]);
      }
      blockedCount--;
      return;
    }
  }
}

void FocusDNS::clearBlockList() {
  blockedCount = 0;
  memset(blockedDomains, 0, sizeof(blockedDomains));
}

int FocusDNS::getBlockedCount() const {
  return blockedCount;
}

const char* FocusDNS::getBlockedDomain(int index) const {
  if (index < 0 || index >= blockedCount) return "";
  return blockedDomains[index];
}

bool FocusDNS::shouldBlockDomain(const char* query) {
  String domain = String(query);
  domain.toLowerCase();

  for (int i = 0; i < blockedCount; i++) {
    if (matchDomain(domain.c_str(), blockedDomains[i])) {
      return true;
    }
  }
  return false;
}

bool FocusDNS::matchDomain(const char* query, const char* pattern) {
  // Exact match
  if (strcmp(query, pattern) == 0) return true;

  // Suffix match for subdomains
  // "facebook.com" matches "www.facebook.com", "m.facebook.com"
  int queryLen = strlen(query);
  int patternLen = strlen(pattern);

  if (queryLen > patternLen) {
    const char* suffix = query + (queryLen - patternLen);
    if (strcmp(suffix, pattern) == 0) {
      // Verify dot before pattern
      if (query[queryLen - patternLen - 1] == '.') {
        return true;
      }
    }
  }

  return false;
}

String FocusDNS::parseDomainFromQuery(uint8_t* buffer, int len) {
  // DNS header is 12 bytes, domain starts at byte 12
  String domain = "";
  int pos = 12;

  while (pos < len && buffer[pos] != 0) {
    int labelLen = buffer[pos];
    if (labelLen > 63 || pos + labelLen >= len) break;

    if (domain.length() > 0) domain += ".";

    for (int i = 0; i < labelLen; i++) {
      domain += (char)buffer[pos + 1 + i];
    }
    pos += labelLen + 1;
  }

  domain.toLowerCase();
  return domain;
}

void FocusDNS::sendBlockedResponse(uint8_t* query, int queryLen, IPAddress remoteIP, uint16_t remotePort) {
  // Build DNS response with 0.0.0.0
  uint8_t response[512];
  memcpy(response, query, queryLen);

  // Set response flags
  response[2] = 0x81;  // QR=1 (response), Opcode=0, AA=0, TC=0, RD=1
  response[3] = 0x80;  // RA=1, Z=0, RCODE=0 (no error)

  // Answer count = 1
  response[6] = 0x00;
  response[7] = 0x01;

  // Copy question section, then add answer
  int answerStart = queryLen;

  // Name pointer to question
  response[answerStart] = 0xC0;
  response[answerStart + 1] = 0x0C;

  // Type A (1)
  response[answerStart + 2] = 0x00;
  response[answerStart + 3] = 0x01;

  // Class IN (1)
  response[answerStart + 4] = 0x00;
  response[answerStart + 5] = 0x01;

  // TTL (60 seconds)
  response[answerStart + 6] = 0x00;
  response[answerStart + 7] = 0x00;
  response[answerStart + 8] = 0x00;
  response[answerStart + 9] = 0x3C;

  // Data length (4 bytes for IP)
  response[answerStart + 10] = 0x00;
  response[answerStart + 11] = 0x04;

  // IP: 0.0.0.0 (blocked)
  response[answerStart + 12] = 0x00;
  response[answerStart + 13] = 0x00;
  response[answerStart + 14] = 0x00;
  response[answerStart + 15] = 0x00;

  int responseLen = answerStart + 16;

  dnsUdp.beginPacket(remoteIP, remotePort);
  dnsUdp.write(response, responseLen);
  dnsUdp.endPacket();
}

void FocusDNS::sendCaptiveResponse(uint8_t* query, int queryLen, IPAddress remoteIP, uint16_t remotePort) {
  // Same as blocked but return captive portal IP
  uint8_t response[512];
  memcpy(response, query, queryLen);

  response[2] = 0x81;
  response[3] = 0x80;
  response[6] = 0x00;
  response[7] = 0x01;

  int answerStart = queryLen;

  response[answerStart] = 0xC0;
  response[answerStart + 1] = 0x0C;
  response[answerStart + 2] = 0x00;
  response[answerStart + 3] = 0x01;
  response[answerStart + 4] = 0x00;
  response[answerStart + 5] = 0x01;
  response[answerStart + 6] = 0x00;
  response[answerStart + 7] = 0x00;
  response[answerStart + 8] = 0x00;
  response[answerStart + 9] = 0x3C;
  response[answerStart + 10] = 0x00;
  response[answerStart + 11] = 0x04;

  // Captive portal IP
  response[answerStart + 12] = captiveIP[0];
  response[answerStart + 13] = captiveIP[1];
  response[answerStart + 14] = captiveIP[2];
  response[answerStart + 15] = captiveIP[3];

  int responseLen = answerStart + 16;

  dnsUdp.beginPacket(remoteIP, remotePort);
  dnsUdp.write(response, responseLen);
  dnsUdp.endPacket();
}

uint32_t FocusDNS::getBlockedQueryCount() const { return blockedQueries; }
uint32_t FocusDNS::getTotalQueryCount() const { return totalQueries; }
void FocusDNS::resetStats() { blockedQueries = 0; totalQueries = 0; }
