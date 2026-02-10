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
  bool inDescription;  // For multi-line DESCRIPTION handling

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

private:
  TaskyModel *model;
  ApiClient api;
  uint32_t lastUpdate = 0;

  // Streaming parser - processes one line at a time
  static bool parseICalLine(const char* line, void* userData);
  void processEvent(ICalParseState& state);
};
