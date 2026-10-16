#pragma once

#include "api_client.h"
#include "model.h"

// State for streaming iCal parser (line by line, minimal RAM)
struct ICalParseState {
  TaskyModel* model;

  // Current date info (for filtering events)
  int currentYear;
  int currentMonth;
  int currentDay;
  int8_t tzOffset;  // UTC offset in hours (from model->cfg.timezone)

  // Parser state machine
  bool inEvent;          // Inside VEVENT block
  int eventsFound;

  // Current event data (temporary, small buffers)
  int eventYear, eventMonth, eventDay;
  int eventHour, eventMinute;
  int eventEndHour, eventEndMinute;  // DTEND time
  char eventSummary[64];  // Max 63 chars + null
  char eventDescription[65];  // DESCRIPTION (64 chars + null)
  bool hasDate;
  bool hasTime;
  bool hasEndTime;
  bool isUtc;       // DTSTART has 'Z' suffix (needs timezone conversion)
  bool inDescription;  // For multi-line DESCRIPTION handling

  // RRULE data (basic recurrence support)
  bool hasRRule;
  char rruleFreq;    // 'D'=daily, 'W'=weekly, 'M'=monthly, 0=none
  int rruleInterval;  // Interval (default 1)
  int rruleCount;     // COUNT limit (-1 = no limit)
  bool rruleUntilSet;
  int rruleUntilYear, rruleUntilMonth, rruleUntilDay;

  // Next event tracking
  long minDaysDiff;
  char nextSummary[64];
  char nextTime[8];
};

class CalendarService {
public:
  CalendarService(TaskyModel *model);

  void begin();
  void loop(); // Call periodically
  void forceUpdate();
  // Result of the last forceUpdate(), for the web setup's "Test it" button
  bool lastOk = false;
  uint32_t lastAttemptAt = 0;  // millis() when forceUpdate() last finished
  String lastError;
  int lastEvents = 0;
  void requestRefresh() {  // fetch on the next loop() (settings changed)
    lastUpdate = 0;
    lastAttemptAt = 0;
  }

private:
  TaskyModel *model;
  ApiClient api;
  uint32_t lastUpdate = 0;

  // Streaming parser - processes one line at a time
  static bool parseICalLine(const char* line, void* userData);
  void processEvent(ICalParseState& state);
};
