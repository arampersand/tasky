Tasky Feature Modernization Phase 2
Goal
Replace the redundant "Clock" screen with a "Productive Time" stats screen, implement battery monitoring, and outline the path for advanced Internet/Bluetooth connectivity.

User Review Required
IMPORTANT

Clock Removal: The manual clock is being completely removed in favor of a "Stats" screen showing session/lifetime focus time. Hardware Dependency: Battery reading requires a voltage divider on a specific PIN. For XIAO ESP32C3, we need to confirm if one exists or if we use an external pin (A0/D0). Assumption: Usage of internal capability or standard pin.

Proposed Changes
1. Stats Screen (Replacing Clock)
File: 
ui.h
, 
ui.cpp
, 
TaskyCircular.ino
, 
model.h

Model: Add uint32_t totalFocusMs to 
TaskyModel
. Save/Load this value.
Logic: Increment totalFocusMs whenever the Timer is in PHASE_WORK and running.
UI: Rename 
drawClock
 to drawStats.
Display "Total Focus": HH:MM layout.
Subtext: "Session: MM min" (current session).
2. Battery Monitoring
File: 
TaskyCircular.ino
, 
ui.cpp

Logic:
Read analogRead(A0) (or defined BAT_PIN).
XIAO ESP32C3 does support battery reading on pin A0 (D0) if wired, or A2 (D2) via divider. Self-Correction: The XIAO ESP32C3 battery pad is connected to A0(D0) via a divider (usually). We will use A0.
Formula: voltage = reading * 3.3 / 4095 * 2 (if 1/2 divider).
UI: Small icon in the top/bottom rim of 
drawFrame
 or a dedicated item in Settings.
3. Future Roadmap: Connectivity (Note Only)
Google/GitHub: Requires OAuth2. Hard on MCU directly.
Strategy A (Direct): ESP32 uses HTTPS to call APIs directly. Hard to manage tokens.
Strategy B (Proxy/Companion):
App-based: Phone app (React Native?) talks to Google/GitHub, sends summarized data to Tasky via BLE.
Server-based: Tasky connects to "TaskyCloud", which handles OAuth.
Recommendation: BLE Companion App or Server Proxy. Direct HTTPS is possible but token refresh is painful on-device.
Implementation Steps
Model Update: Add totalFocusMs.
UI Refactor: SCR_CLOCK -> SCR_STATS.
Battery Logic: Add readBattery() helper. Implement icon.
Documentation: Write roadmap for user.