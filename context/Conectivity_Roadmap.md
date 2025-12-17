Tasky Connectivity Roadmap
Goal
Integrate Google Calendar (Tasks) and GitHub (Streak) into Tasky using the ESP32-C3's WiFi and Bluetooth capabilities.

Challenges
Authentication (OAuth2): Google and GitHub require complex OAuth2 flows (redirect URLs, token exchanges) which are difficult to host on a microcontroller.
HTTPS/TLS: Verifying SSL certificates requires significant RAM and processing power.
Battery Life: WiFi consumes ~100-200mA, depleting the battery quickly if used constantly.
Recommended Strategies
Strategy A: The "Companion App" (Best for Battery)
Use a mobile app (iOS/Android) or Desktop bridge as a gateway.

Phone/PC connects to Google/GitHub and handles auth.
Tasky connects to Phone via BLE (Bluetooth Low Energy).
Device sends data to Tasky: {"tasks": ["Buy Milk", "Code"], "streak": 5}.
Pros: Ultra-low power (BLE), easy auth on phone.
Cons: Requires developing a separate mobile app.
Strategy B: The "Server Proxy" (Best for Standalone)
Use a cloud server (AWS Lambda / Vercel) to handle heavy lifting.

Tasky connects to WiFi.
Tasky calls a simple HTTP endpoint: api.tasky.com/sync?device_id=123.
Cloud Server holds the OAuth tokens, fetches data from Google/GitHub, and returns a clean JSON summary to Tasky.
Pros: Device works independently of phone.
Cons: Higher battery usage (WiFi), requires server maintenance.
Implementation Steps (Phase 3)
WiFi Optimization: Implement "Deep Sleep" with periodic wake-ups (e.g., sync every hour). Update 
Tasky
 to only turn on WiFi when requested.
JSON Parser: Add ArduinoJson library to parse API responses.
Token Management: Store "Refresh Tokens" in Preferences (EEPROM) if going with Strategy B.
Immediate Next Step
Battery Optimization: Before adding WiFi sync, ensure the device enters Light Sleep when the timer is idle to extend battery life.
Prototype: Create a simple "GET" request to a public API (e.g., world time) to verify HTTPS stability on the circular display setup.