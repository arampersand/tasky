#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <WiFi.h>
#include "model.h"

// Maximum domains in block list
#define MAX_BLOCKED_DOMAINS 16
#define MAX_DOMAIN_LENGTH 32
#define DNS_PORT 53

class FocusDNS {
public:
  FocusDNS(TaskyModel* m);

  // Lifecycle
  void begin(IPAddress apIP);
  void stop();
  void loop();

  // Focus mode control
  void setFocusMode(bool enabled);
  bool isFocusModeActive() const;

  // Block list management
  void addBlockedDomain(const char* domain);
  void removeBlockedDomain(const char* domain);
  void clearBlockList();
  void loadDefaultBlockList();
  int getBlockedCount() const;
  const char* getBlockedDomain(int index) const;

  // Statistics
  uint32_t getBlockedQueryCount() const;
  uint32_t getTotalQueryCount() const;
  void resetStats();

private:
  TaskyModel* model;
  WiFiUDP dnsUdp;
  IPAddress captiveIP;

  // Focus mode state
  bool focusModeEnabled;
  bool serverRunning;

  // Block list (stored in RAM for fast matching)
  char blockedDomains[MAX_BLOCKED_DOMAINS][MAX_DOMAIN_LENGTH];
  int blockedCount;

  // Statistics
  uint32_t blockedQueries;
  uint32_t totalQueries;

  // DNS handling
  bool shouldBlockDomain(const char* domain);
  bool matchDomain(const char* query, const char* pattern);
  String parseDomainFromQuery(uint8_t* buffer, int len);
  void sendBlockedResponse(uint8_t* query, int queryLen, IPAddress remoteIP, uint16_t remotePort);
  void sendCaptiveResponse(uint8_t* query, int queryLen, IPAddress remoteIP, uint16_t remotePort);
};
