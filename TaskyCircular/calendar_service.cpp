#include "calendar_service.h"
#include <Arduino.h>

CalendarService::CalendarService(TaskyModel *m) : model(m) {}

void CalendarService::begin() {
  // Initial delay or immediate?
}

void CalendarService::loop() {
  if (model->cfg.wifiSSID.length() == 0 || model->cfg.calendarUrl.length() == 0)
    return;

  // Update every 15 minutes
  if (millis() - lastUpdate > 15 * 60 * 1000) {
    forceUpdate();
  }
}

void CalendarService::forceUpdate() {
  if (model->cfg.calendarUrl.length() == 0)
    return;

  Serial.println("[Cal] Updating...");
  lastUpdate = millis();

  String payload = api.get(model->cfg.calendarUrl);
  if (payload.length() > 0) {
    parseICal(payload);
  } else {
    Serial.println("[Cal] Failed to fetch");
  }
}

// Simple parser for next event
void CalendarService::parseICal(String data) {
  Serial.println("[Cal] Parsing...");

  // We need to find VEVENTs, check DTSTART.
  // Compare with current time?
  // Model clock is H:M:S. We don't track full date in model yet?
  // Ah, NTP syncs time, but do we have Date?
  // struct tm timeinfo; getLocalTime(&timeinfo); has date.

  struct tm now;
  if (!getLocalTime(&now)) {
    Serial.println("[Cal] No system time available");
    return;
  }

  // Basic Search:
  // Find "BEGIN:VEVENT"
  // Find "DTSTART"
  // Find "SUMMARY"

  // This is a naive parser. For robust parsing, we need a library or better
  // logic. For prototype: Just find the FIRST event that is "today" or
  // "future". Note: iCal events might not be sorted.

  int idx = 0;
  String winningSummary = "No Events";
  String winningTime = "--:--";
  long minDiff = 2147483647; // Max long

  // Current time in comparable format (e.g., minutes from now, or just
  // YYYYMMDDHHMM) iCal date format: YYYYMMDDTHHMMSSZ or TZID...

  // Optimization: If the user just wants the *next* thing today.

  // Let's assume simplistic "Find first future event" for now.
  // Real implementation needs full iCal state machine.

  // Placeholder logic:
  if (data.indexOf("SUMMARY:") > 0) {
    int s = data.indexOf("SUMMARY:");
    int e = data.indexOf("\n", s);
    winningSummary = data.substring(s + 8, e);
    winningSummary.trim();
    winningTime = "Soon"; // Placeholder until we parse DTSTART
  }

  model->cfg.nextEventSummary = winningSummary;
  model->cfg.nextEventTime = winningTime;
  Serial.println("[Cal] Updated: " + winningSummary);
}
