#include "config_features.h"
#if FEATURE_CALENDAR
#include "calendar_service.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

CalendarService::CalendarService(TaskyModel *m) : model(m) {}

void CalendarService::begin() {
  // Initial delay or immediate?
}

void CalendarService::loop() {
  if (model->cfg.wifiSSID.length() == 0 || model->cfg.calendarUrl.length() == 0)
    return;

  // First update: try after 10 seconds (wait for WiFi), then every 15 minutes
  uint32_t interval = (lastUpdate == 0) ? 10000 : (15UL * 60 * 1000);
  if (millis() - lastUpdate > interval) {
    forceUpdate();
  }
}

// Helper: days in a given month (1-12)
static int calDaysInMonth(int month, int year) {
  const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 30;
  int days = d[month - 1];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
    days = 29;
  return days;
}

// Helper: day of week (0=Sun, 1=Mon, ..., 6=Sat) for a given date
static int calDayOfWeek(int year, int month, int day) {
  // Tomohiko Sakamoto's algorithm
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 3) year--;
  return (year + year/4 - year/100 + year/400 + t[month-1] + day) % 7;
}

// Helper: convert UTC date/time to local by applying timezone offset
static void applyTimezone(int& year, int& month, int& day, int& hour, int minute, int8_t tzOffset) {
  if (tzOffset == 0) return;
  hour += tzOffset;
  if (hour < 0) {
    hour += 24;
    day--;
    if (day < 1) {
      month--;
      if (month < 1) { month = 12; year--; }
      day = calDaysInMonth(month, year);
    }
  } else if (hour >= 24) {
    hour -= 24;
    day++;
    int dim = calDaysInMonth(month, year);
    if (day > dim) { day = 1; month++; if (month > 12) { month = 1; year++; } }
  }
}

// Helper: register an event occurrence on a specific day
static void registerOccurrence(ICalParseState* state, int day, int hour, int minute,
                                int endHour, int endMinute, bool hasTime, bool hasEndTime) {
  // Mark calendar grid
  if (day >= 1 && day <= 31) {
    int idx = day - 1;
    if (state->model->calendarEvents[idx] < 3) {
      state->model->calendarEvents[idx]++;
    }
    state->eventsFound++;

    // Store in monthEvents (for any day's detail view)
    if (state->model->monthEventCount < MAX_MONTH_EVENTS) {
      CalendarEventItem& mItem = state->model->monthEvents[state->model->monthEventCount];

      strncpy(mItem.title, state->eventSummary, 32);
      mItem.title[32] = '\0';
      strncpy(mItem.description, state->eventDescription, 64);
      mItem.description[64] = '\0';

      if (hasTime && hour >= 0) {
        snprintf(mItem.startTime, 6, "%02d:%02d", hour, minute);
        mItem.isAllDay = false;
      } else {
        strcpy(mItem.startTime, "--:--");
        mItem.isAllDay = true;
      }

      if (hasEndTime && endHour >= 0) {
        snprintf(mItem.endTime, 6, "%02d:%02d", endHour, endMinute);
      } else {
        strcpy(mItem.endTime, "--:--");
      }

      mItem.day = day;
      state->model->monthEventCount++;
    }

    // Also store today's events for backward compatibility (agenda screen)
    if (day == state->currentDay &&
        state->model->todayEventCount < MAX_TODAY_EVENTS) {
      CalendarEventItem& item = state->model->todayEvents[state->model->todayEventCount];

      strncpy(item.title, state->eventSummary, 32);
      item.title[32] = '\0';
      strncpy(item.description, state->eventDescription, 64);
      item.description[64] = '\0';

      if (hasTime && hour >= 0) {
        snprintf(item.startTime, 6, "%02d:%02d", hour, minute);
        item.isAllDay = false;
      } else {
        strcpy(item.startTime, "--:--");
        item.isAllDay = true;
      }

      if (hasEndTime && endHour >= 0) {
        snprintf(item.endTime, 6, "%02d:%02d", endHour, endMinute);
      } else {
        strcpy(item.endTime, "--:--");
      }

      item.day = day;
      state->model->todayEventCount++;
    }
  }
}

// Helper: expand RRULE occurrences for the current month
static void expandRRule(ICalParseState* state) {
  if (!state->hasRRule || state->rruleFreq == 0) return;
  if (!state->hasDate) return;

  int origYear = state->eventYear;
  int origMonth = state->eventMonth;
  int origDay = state->eventDay;
  int interval = state->rruleInterval > 0 ? state->rruleInterval : 1;
  int origDow = calDayOfWeek(origYear, origMonth, origDay);

  int targetYear = state->currentYear;
  int targetMonth = state->currentMonth;
  int daysInTarget = calDaysInMonth(targetMonth, targetYear);

  // Check UNTIL limit
  if (state->rruleUntilSet) {
    // If UNTIL is before current month, skip
    if (state->rruleUntilYear < targetYear ||
        (state->rruleUntilYear == targetYear && state->rruleUntilMonth < targetMonth)) {
      return;
    }
  }

  // The original occurrence was already registered if it falls in current month
  // Now generate additional occurrences

  if (state->rruleFreq == 'D') {
    // DAILY: check each day of current month
    for (int d = 1; d <= daysInTarget; d++) {
      if (d == origDay && origMonth == targetMonth && origYear == targetYear)
        continue;  // Already registered

      // Calculate days since original
      // Simplified: convert to a linear day count
      long origDays = (long)origYear * 365 + origYear/4 - origYear/100 + origYear/400;
      for (int m = 1; m < origMonth; m++) origDays += calDaysInMonth(m, origYear);
      origDays += origDay;

      long testDays = (long)targetYear * 365 + targetYear/4 - targetYear/100 + targetYear/400;
      for (int m = 1; m < targetMonth; m++) testDays += calDaysInMonth(m, targetYear);
      testDays += d;

      long diff = testDays - origDays;
      if (diff < 0) continue;  // Before original event
      if (diff % interval != 0) continue;  // Not on interval

      // Check UNTIL
      if (state->rruleUntilSet) {
        if (targetYear > state->rruleUntilYear) continue;
        if (targetYear == state->rruleUntilYear && targetMonth > state->rruleUntilMonth) continue;
        if (targetYear == state->rruleUntilYear && targetMonth == state->rruleUntilMonth &&
            d > state->rruleUntilDay) continue;
      }

      registerOccurrence(state, d, state->eventHour, state->eventMinute,
                         state->eventEndHour, state->eventEndMinute,
                         state->hasTime, state->hasEndTime);
    }
  }
  else if (state->rruleFreq == 'W') {
    // WEEKLY: check each day of current month that matches the day-of-week
    int targetDow = origDow;  // Same day of week as original

    for (int d = 1; d <= daysInTarget; d++) {
      int dow = calDayOfWeek(targetYear, targetMonth, d);
      if (dow != targetDow) continue;

      // Skip original occurrence (already registered)
      if (d == origDay && origMonth == targetMonth && origYear == targetYear)
        continue;

      // Check it's after original date
      if (targetYear < origYear) continue;
      if (targetYear == origYear && targetMonth < origMonth) continue;
      if (targetYear == origYear && targetMonth == origMonth && d < origDay) continue;

      // Check interval (count weeks since original)
      long origDays = (long)origYear * 365 + origYear/4 - origYear/100 + origYear/400;
      for (int m = 1; m < origMonth; m++) origDays += calDaysInMonth(m, origYear);
      origDays += origDay;

      long testDays = (long)targetYear * 365 + targetYear/4 - targetYear/100 + targetYear/400;
      for (int m = 1; m < targetMonth; m++) testDays += calDaysInMonth(m, targetYear);
      testDays += d;

      long weeksDiff = (testDays - origDays) / 7;
      if (weeksDiff < 0) continue;
      if (interval > 1 && weeksDiff % interval != 0) continue;

      // Check UNTIL
      if (state->rruleUntilSet) {
        if (targetYear > state->rruleUntilYear) continue;
        if (targetYear == state->rruleUntilYear && targetMonth > state->rruleUntilMonth) continue;
        if (targetYear == state->rruleUntilYear && targetMonth == state->rruleUntilMonth &&
            d > state->rruleUntilDay) continue;
      }

      registerOccurrence(state, d, state->eventHour, state->eventMinute,
                         state->eventEndHour, state->eventEndMinute,
                         state->hasTime, state->hasEndTime);
    }
  }
  else if (state->rruleFreq == 'M') {
    // MONTHLY: same day each month
    if (targetYear < origYear) return;
    if (targetYear == origYear && targetMonth < origMonth) return;
    // Skip if this IS the original month
    if (targetYear == origYear && targetMonth == origMonth) return;

    // Check interval
    int monthsDiff = (targetYear - origYear) * 12 + (targetMonth - origMonth);
    if (interval > 1 && monthsDiff % interval != 0) return;

    int day = origDay;
    if (day > daysInTarget) day = daysInTarget;  // Clamp (e.g., Jan 31 -> Feb 28)

    // Check UNTIL
    if (state->rruleUntilSet) {
      if (targetYear > state->rruleUntilYear) return;
      if (targetYear == state->rruleUntilYear && targetMonth > state->rruleUntilMonth) return;
      if (targetYear == state->rruleUntilYear && targetMonth == state->rruleUntilMonth &&
          day > state->rruleUntilDay) return;
    }

    registerOccurrence(state, day, state->eventHour, state->eventMinute,
                       state->eventEndHour, state->eventEndMinute,
                       state->hasTime, state->hasEndTime);
  }
}

void CalendarService::forceUpdate() {
  lastOk = false;
  lastEvents = 0;
  if (model->cfg.calendarUrl.length() == 0) {
    lastError = "No calendar link saved";
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    lastError = "Tasky isn't on Wi-Fi yet";
    return;
  }

  Serial.println("[Cal] Updating (streaming)...");

  // Non-blocking time check
  time_t nowTime = time(nullptr);
  if (nowTime < 1577836800) {  // Before year 2020 = NTP not synced
    Serial.println("[Cal] No system time available");
    lastError = "Tasky doesn't know today's date yet";
    return;
  }

  struct tm now;
  localtime_r(&nowTime, &now);  // Non-blocking conversion

  // Clear calendar events
  for (int i = 0; i < 31; i++) {
    model->calendarEvents[i] = 0;
  }

  // Clear today's events and month events
  model->clearTodayEvents();
  model->clearMonthEvents();

  // Initialize parser state
  ICalParseState state;
  memset(&state, 0, sizeof(state));
  state.model = model;
  state.currentYear = now.tm_year + 1900;
  state.currentMonth = now.tm_mon + 1;  // 0-11 -> 1-12
  state.currentDay = now.tm_mday;
  state.tzOffset = model->cfg.timezone;
  state.inEvent = false;
  state.inDescription = false;
  state.eventsFound = 0;
  state.minDaysDiff = 2147483647;
  strcpy(state.nextSummary, "No Events");
  strcpy(state.nextTime, "--:--");

  Serial.printf("[Cal] Today: %d-%02d-%02d, TZ offset: %d\n",
                state.currentYear, state.currentMonth, state.currentDay, state.tzOffset);

  // Stream and parse
  bool success = api.getStreaming(model->cfg.calendarUrl, parseICalLine, &state);

  if (success) {
    // Copy results to model
    model->cfg.nextEventSummary = String(state.nextSummary);
    model->cfg.nextEventTime = String(state.nextTime);
    lastUpdate = millis();  // Only mark success time
    lastOk = true;
    lastEvents = state.eventsFound;
    lastError = "";

    Serial.printf("[Cal] Found %d events this month, %d today, %d stored in monthEvents\n",
                  state.eventsFound, model->todayEventCount, model->monthEventCount);
    Serial.printf("[Cal] Next: %s at %s\n", state.nextSummary, state.nextTime);
    lastAttemptAt = millis();
  } else {
    Serial.println("[Cal] Stream failed - will retry in 30s");
    int code = api.lastStatus;
    lastError = code <= 0  ? String("Couldn't reach the calendar server")
                : code == 404 || code == 403 || code == 401
                    ? "Couldn't download it (" + String(code) + "). Copy the link again: it may be private or old"
                    : "Couldn't download it (" + String(code) + ")";
    lastAttemptAt = millis();
    Serial.printf("[Cal] URL: %s\n", redactUrl(model->cfg.calendarUrl).c_str());
    lastUpdate = millis() - (15UL * 60 * 1000) + 30000;  // Retry in 30s
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
    Serial.println("[Cal] Found VEVENT");
    state->inEvent = true;
    state->inDescription = false;
    // Reset current event data
    state->eventYear = 0;
    state->eventMonth = 0;
    state->eventDay = 0;
    state->eventHour = -1;
    state->eventMinute = -1;
    state->eventEndHour = -1;
    state->eventEndMinute = -1;
    state->eventSummary[0] = '\0';
    state->eventDescription[0] = '\0';
    state->hasDate = false;
    state->hasTime = false;
    state->hasEndTime = false;
    state->isUtc = false;
    // Reset RRULE
    state->hasRRule = false;
    state->rruleFreq = 0;
    state->rruleInterval = 1;
    state->rruleCount = -1;
    state->rruleUntilSet = false;
    return true;
  }

  // Check for END:VEVENT
  if (strncmp(line, "END:VEVENT", 10) == 0) {
    state->inDescription = false;
    if (state->inEvent && state->hasDate) {
      // Apply UTC->local timezone conversion if needed
      if (state->isUtc && state->hasTime && state->tzOffset != 0) {
        applyTimezone(state->eventYear, state->eventMonth, state->eventDay,
                      state->eventHour, state->eventMinute, state->tzOffset);
        // Also convert end time
        if (state->hasEndTime && state->eventEndHour >= 0) {
          int tmpYear = state->eventYear, tmpMonth = state->eventMonth, tmpDay = state->eventDay;
          applyTimezone(tmpYear, tmpMonth, tmpDay,
                        state->eventEndHour, state->eventEndMinute, state->tzOffset);
        }
      }

      // Check if event is in current month (original occurrence)
      if (state->eventYear == state->currentYear &&
          state->eventMonth == state->currentMonth) {
        registerOccurrence(state, state->eventDay,
                           state->eventHour, state->eventMinute,
                           state->eventEndHour, state->eventEndMinute,
                           state->hasTime, state->hasEndTime);
      }

      // Track next upcoming event (original occurrence)
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

      // Expand RRULE recurrences for the current month
      if (state->hasRRule) {
        expandRRule(state);
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
    state->inDescription = false;
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

        // Check for UTC 'Z' suffix
        int len = strlen(dateStart);
        if (dateStart[len - 1] == 'Z') {
          state->isUtc = true;
        }
      }

      // Check if TZID is present (not UTC, local time assumed)
      if (strstr(line, "TZID=") != nullptr) {
        state->isUtc = false;  // Already local time
      }
    }
    return true;
  }

  // Parse RRULE line
  // Format: RRULE:FREQ=WEEKLY;INTERVAL=1;UNTIL=20261231T000000Z
  if (strncmp(line, "RRULE:", 6) == 0) {
    state->inDescription = false;
    state->hasRRule = true;
    const char* rule = line + 6;

    // Parse FREQ
    const char* freq = strstr(rule, "FREQ=");
    if (freq) {
      freq += 5;
      if (strncmp(freq, "DAILY", 5) == 0) state->rruleFreq = 'D';
      else if (strncmp(freq, "WEEKLY", 6) == 0) state->rruleFreq = 'W';
      else if (strncmp(freq, "MONTHLY", 7) == 0) state->rruleFreq = 'M';
      else if (strncmp(freq, "YEARLY", 6) == 0) state->rruleFreq = 'Y';  // Skip yearly
    }

    // Parse INTERVAL
    const char* intv = strstr(rule, "INTERVAL=");
    if (intv) {
      state->rruleInterval = atoi(intv + 9);
      if (state->rruleInterval < 1) state->rruleInterval = 1;
    }

    // Parse COUNT
    const char* cnt = strstr(rule, "COUNT=");
    if (cnt) {
      state->rruleCount = atoi(cnt + 6);
    }

    // Parse UNTIL
    const char* until = strstr(rule, "UNTIL=");
    if (until) {
      until += 6;
      if (strlen(until) >= 8) {
        char uYear[5] = {until[0], until[1], until[2], until[3], '\0'};
        char uMonth[3] = {until[4], until[5], '\0'};
        char uDay[3] = {until[6], until[7], '\0'};
        state->rruleUntilYear = atoi(uYear);
        state->rruleUntilMonth = atoi(uMonth);
        state->rruleUntilDay = atoi(uDay);
        state->rruleUntilSet = true;
      }
    }

    Serial.printf("[Cal] RRULE: freq=%c interval=%d\n", state->rruleFreq, state->rruleInterval);
    return true;
  }

  // Parse SUMMARY line
  if (strncmp(line, "SUMMARY:", 8) == 0) {
    state->inDescription = false;
    const char* summary = line + 8;
    strncpy(state->eventSummary, summary, 63);
    state->eventSummary[63] = '\0';
    return true;
  }

  // Parse DESCRIPTION line
  if (strncmp(line, "DESCRIPTION:", 12) == 0) {
    const char* desc = line + 12;
    strncpy(state->eventDescription, desc, 64);
    state->eventDescription[64] = '\0';
    state->inDescription = true;
    return true;
  }

  // Handle line folding (continuation lines start with space or tab)
  if (state->inDescription && (line[0] == ' ' || line[0] == '\t')) {
    // Append to description (with length check)
    size_t current = strlen(state->eventDescription);
    if (current < 63) {
      strncat(state->eventDescription, line + 1, 63 - current);
      state->eventDescription[64] = '\0';
    }
    return true;
  } else if (line[0] != ' ' && line[0] != '\t') {
    // New property, stop description continuation
    state->inDescription = false;
  }

  // Parse DTEND line (similar to DTSTART)
  if (strncmp(line, "DTEND", 5) == 0) {
    const char* dateStart = strrchr(line, ':');
    if (dateStart && strlen(dateStart) >= 9) {
      dateStart++;
      // Check for time (T follows date at position 8)
      if (strlen(dateStart) >= 13 && dateStart[8] == 'T') {
        char hourStr[3] = {dateStart[9], dateStart[10], '\0'};
        char minStr[3] = {dateStart[11], dateStart[12], '\0'};
        state->eventEndHour = atoi(hourStr);
        state->eventEndMinute = atoi(minStr);
        state->hasEndTime = true;

        // Check for UTC 'Z' suffix
        int len = strlen(dateStart);
        if (dateStart[len - 1] == 'Z') {
          // End time is UTC too, will be converted along with start
          state->isUtc = true;
        }
      }
    }
    return true;
  }

  return true;  // Continue processing
}
#endif
