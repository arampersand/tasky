Tasky Firmware - Connectivity & Integrations
[/] Core Connectivity (WiFi Station)

 Create 
NetworkManager
 class to handle connection logic (Auto-connect, Re-connect).
 Integrate 
NetworkManager
 with 
TaskyModel
 (use saved credentials).
 Update 
WiFiPortal
 to allow testing the connection (Save & Test).
 UI: Add status icons/indicators for WiFi state (Connecting, Connected, Offline).
 System Time (NTP)

 Implement NTP synchronization when WiFi is connected.
 seamless update of the internal RTC/millis clock.
[/] Integration Architecture (API Client)

 Design generic 
ApiClient
 for HTTPS requests.
 Define storage for API Keys (GitHub Token, Calendar URL).
 Add fields to Web Portal for entering these keys.
 GitHub Integration (Prototype)

 Implement simple fetch of User contribution graph or events.
 Map contribution count to "Streak" data in 
TaskyModel
.
 Calendar Integration

 Model: Add nextEvent fields.
 UI: Create SCR_CALENDAR and 
drawCalendar()
 screen.
 Logic: Create 
CalendarService
 to fetch and parse iCal data using 
ApiClient
.
 Navigation: Add to main loop cycle.
[/] GitHub Integration

 Model: Add 
streak
 array mapping/logic.
 Logic: Create GithubService (fetch contributions, parse HTML/JSON).
 UI: Connect SCR_STREAK to real data.