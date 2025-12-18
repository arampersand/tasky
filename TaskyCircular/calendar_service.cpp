#include "calendar_service.h"
#include <Arduino.h>
#include <cstring>

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

  Serial.println("[Cal] Updating (streaming)...");
  lastUpdate = millis();

  // Non-blocking time check
  time_t nowTime = time(nullptr);
  if (nowTime < 1577836800) {  // Before year 2020 = NTP not synced
    Serial.println("[Cal] No system time available");
    return;
  }

  struct tm now;
  localtime_r(&nowTime, &now);  // Non-blocking conversion

  // Clear calendar events
  for (int i = 0; i < 31; i++) {
    model->calendarEvents[i] = 0;
  }

  // Initialize parser state
  ICalParseState state;
  memset(&state, 0, sizeof(state));
  state.model = model;
  state.currentYear = now.tm_year + 1900;
  state.currentMonth = now.tm_mon + 1;  // 0-11 -> 1-12
  state.currentDay = now.tm_mday;
  state.inEvent = false;
  state.eventsFound = 0;
  state.minDaysDiff = 2147483647;
  strcpy(state.nextSummary, "No Events");
  strcpy(state.nextTime, "--:--");

  // Stream and parse
  bool success = api.getStreaming(model->cfg.calendarUrl, parseICalLine, &state);

  if (success) {
    // Copy results to model
    model->cfg.nextEventSummary = String(state.nextSummary);
    model->cfg.nextEventTime = String(state.nextTime);

    Serial.printf("[Cal] Found %d events this month\n", state.eventsFound);
    Serial.printf("[Cal] Next: %s at %s\n", state.nextSummary, state.nextTime);
  } else {
    Serial.println("[Cal] Stream failed");
  }
}

// Static callback for line-by-line parsing
bool CalendarService::parseICalLine(const char* line, void* userData) {
  ICalParseState* state = (ICalParseState*)userData;

  // Skip empty lines
  if (line[0] == '\0') {
    return true;
  }

  // Check for BEGIN:VEVENT
  if (strncmp(line, "BEGIN:VEVENT", 12) == 0) {
    state->inEvent = true;
    // Reset current event data
    state->eventYear = 0;
    state->eventMonth = 0;
    state->eventDay = 0;
    state->eventHour = -1;
    state->eventMinute = -1;
    state->eventSummary[0] = '\0';
    state->hasDate = false;
    state->hasTime = false;
    return true;
  }

  // Check for END:VEVENT
  if (strncmp(line, "END:VEVENT", 10) == 0) {
    if (state->inEvent && state->hasDate) {
      // Process the completed event

      // Check if event is in current month
      if (state->eventYear == state->currentYear &&
          state->eventMonth == state->currentMonth) {
        // Mark this day as having an event
        if (state->eventDay >= 1 && state->eventDay <= 31) {
          int idx = state->eventDay - 1;
          if (state->model->calendarEvents[idx] < 3) {
            state->model->calendarEvents[idx]++;
          }
          state->eventsFound++;
        }
      }

      // Track next upcoming event
      if (state->eventSummary[0] != '\0') {
        long daysDiff = (state->eventYear - state->currentYear) * 365 +
                        (state->eventMonth - state->currentMonth) * 30 +
                        (state->eventDay - state->currentDay);

        if (daysDiff >= 0 && daysDiff < state->minDaysDiff) {
          state->minDaysDiff = daysDiff;
          strncpy(state->nextSummary, state->eventSummary, 63);
          state->nextSummary[63] = '\0';

          if (state->hasTime && state->eventHour >= 0) {
            snprintf(state->nextTime, sizeof(state->nextTime),
                     "%02d:%02d", state->eventHour, state->eventMinute);
          } else {
            strcpy(state->nextTime, "All day");
          }
        }
      }
    }
    state->inEvent = false;
    return true;
  }

  // Only process lines inside VEVENT
  if (!state->inEvent) {
    return true;
  }

  // Parse DTSTART line
  // Formats: DTSTART:20241215T100000Z
  //          DTSTART;VALUE=DATE:20241215
  //          DTSTART;TZID=...:20241215T100000
  if (strncmp(line, "DTSTART", 7) == 0) {
    // Find the date portion (after last ':')
    const char* dateStart = strrchr(line, ':');
    if (dateStart && strlen(dateStart) >= 9) {  // :YYYYMMDD minimum
      dateStart++;  // Skip ':'

      // Parse YYYYMMDD
      char yearStr[5] = {dateStart[0], dateStart[1], dateStart[2], dateStart[3], '\0'};
      char monthStr[3] = {dateStart[4], dateStart[5], '\0'};
      char dayStr[3] = {dateStart[6], dateStart[7], '\0'};

      state->eventYear = atoi(yearStr);
      state->eventMonth = atoi(monthStr);
      state->eventDay = atoi(dayStr);

      // Basic validation
      if (state->eventYear >= 2000 && state->eventYear <= 2100 &&
          state->eventMonth >= 1 && state->eventMonth <= 12 &&
          state->eventDay >= 1 && state->eventDay <= 31) {
        state->hasDate = true;
      }

      // Check for time (T follows date)
      if (strlen(dateStart) >= 13 && dateStart[8] == 'T') {
        char hourStr[3] = {dateStart[9], dateStart[10], '\0'};
        char minStr[3] = {dateStart[11], dateStart[12], '\0'};
        state->eventHour = atoi(hourStr);
        state->eventMinute = atoi(minStr);
        state->hasTime = true;
      }
    }
    return true;
  }

  // Parse SUMMARY line
  if (strncmp(line, "SUMMARY:", 8) == 0) {
    const char* summary = line + 8;
    strncpy(state->eventSummary, summary, 63);
    state->eventSummary[63] = '\0';
    return true;
  }

  return true;  // Continue processing
}
