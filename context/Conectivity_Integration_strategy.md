Connectivity & Integration Strategy
Goal Description
Enable Tasky to connect to the user's home WiFi (Station Mode) and fetch data from external services (GitHub, Calendar) directly. This upgrades the device from a "dumb" timer to a connected productivity tool.

User Decision Required
Architecture Choice: Standalone vs. Companion We recommend Standalone.

Standalone (Recommended)

How it works: Tasky connects to WiFi. You enter your WiFi password and GitHub Token once using the Web Portal (Captive Portal). Tasky saves them and updates itself automatically every hour/day.
Pros: Device is independent. Updates even if your phone is away/off. No custom mobile app required (works with any browser).
Cons: HTTPS is resource heavy (but ESP32-C3 can handle it). Typing passwords on the device is hard (solved by Captive Portal).
Companion (Phone Proxy)

How it works: Tasky stays offline/bluetooth. A custom App on your phone fetches data and pushes it to Tasky.
Pros: "One tap" setup. Phone handles complex logic.
Cons: Requires building and maintaining iOS/Android apps. Device is "dumb" without the phone nearby. High complexity for this stage.
Proposed Changes (Standalone Approach)
1. Network Manager (network_manager.cpp)
New Component: Handles the transition between AP Mode (Config) and Station Mode (Normal Operation).
Logic: On boot, try to connect to saved WiFi. If fail, fall back to AP Mode (or allow manual toggle via UI).
2. Web Portal Updates
New Fields: Add input fields for:
GitHub Username (for public contributions) or API Token (for private).
Google Calendar iCal URL (Secret address).
Test Button: "Save & Connect" to verify WiFi immediately.
3. API Client (api_client.cpp)
Secure Client: Use WiFiClientSecure to poll GitHub/Calendar APIs.
JSON Parsing: Use ArduinoJson (efficient parsing) to extract just what we need (e.g., "commits today: 5").
4. Integration with Model
Auto-Update: Periodic check (e.g., every 15 min or on Docking).
Data Mapping:
GitHub Commits -> Fill TaskyModel.streak array.
Calendar Next Event -> Update TaskyModel.nextEvent string.
Verification Plan
Automated Tests
N/A (Firmware integration requires hardware).
Manual Verification
WiFi Connection: Configure Home WiFi via Portal -> Verify IP address assigned -> Verify "WiFi Icon" on UI.
Portal: Verify new fields save and persist after reboot.
API Test (Future): Hardcode a JSON endpoint and verify Tasky prints the result to Serial.