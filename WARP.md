# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

**Tasky** is an ESP32-C3 based productivity device combining a Pomodoro timer, task tracker, and GitHub-style streak calendar. It features a 240x240 TFT display (ST7789), encoder-based navigation, and WiFi connectivity for web-based configuration and optional integration with external services (GitHub, Calendar).

**Hardware**: Seeed Studio XIAO ESP32-C3 with circular/square TFT display, rotary encoder, LED, buzzer.

## Development Setup

### Requirements
- **Arduino IDE** (tested with ESP32 core 3.3.3)
- **Board**: XIAO ESP32C3
- USB connection for flashing

### Required Arduino Libraries
Install via Library Manager:
- Adafruit GFX Library
- Adafruit ST7789 (for square display)
- Adafruit GC9A01A (for circular display - optional)
- Preferences (ESP32 core)
- WiFi (ESP32 core)
- WebServer (ESP32 core)
- DNSServer (ESP32 core)
- HTTPClient (ESP32 core)

**Note**: The QR code library (qrcodegen.cpp/hpp) is included directly to avoid macro conflicts with ESP32's LOW/HIGH definitions.

## Common Commands

### Build & Upload
Open `TaskyCircular/TaskyCircular.ino` in Arduino IDE:
1. Select Board: **XIAO ESP32C3**
2. Select correct USB port
3. Click **Upload** (Ctrl+U / Cmd+U)

**There is no Makefile or PlatformIO configuration** - this project uses Arduino IDE exclusively.

### Serial Monitor
- Baud rate: **115200**
- Used for debugging network status, WiFi connections, and service updates
- Open with: Tools > Serial Monitor (Ctrl+Shift+M / Cmd+Shift+M)

### Testing
No automated test suite exists. Testing is manual via hardware:
1. Flash firmware to device
2. Navigate screens using encoder/button
3. Verify WiFi portal access
4. Test timer, tasks, streak, settings screens

## Architecture Overview

### Core Components

#### 1. Model Layer (`model.h/cpp`)
Central data model using ESP32 Preferences for persistent storage:
- **Config**: Work/break durations, brightness, WiFi credentials, API tokens
- **Timer State**: Running/paused/finished status, timestamps
- **Tasks**: 3 tasks with completion status
- **Streak Data**: 6x14 grid (84 days) with 0-3 intensity values
- **Clock**: User-adjustable time (no RTC hardware)

Persistence keys are abbreviated (e.g., "wss" = WiFi SSID, "ghu" = GitHub user) to fit NVS constraints.

#### 2. UI Layer (`ui.h/cpp`)
Manages all display rendering using Adafruit GFX:
- **Display Driver Toggle**: Compile-time switch `DISPLAY_CIRCULAR` (line 9 of ui.h) selects ST7789 (square) or GC9A01A (circular)
- **Screens**: Timer, Tasks, Calendar, Streak, Stats, Settings, QR, WiFi Info
- **Animations**: Managed via `animation.h` - typewriter splash, staggered list entry, progress ring, breathing QR
- **Themes**: Dark/light mode with color palette system

**Critical**: Screen coordinate logic differs between square and circular displays - always check the `DISPLAY_CIRCULAR` flag when modifying UI.

#### 3. Network Layer
- **WiFiPortal** (`wifi_portal.h/cpp`): Captive portal for device configuration
  - AP Mode: SSID "Tasky", password "GetItDone"
  - Web interface for setting WiFi, GitHub token, Calendar URL
  - Serves at 192.168.4.1 when AP active
  
- **TaskyNetManager** (`network_manager.h/cpp`): Station mode WiFi connection
  - Auto-connects to saved WiFi on boot
  - Falls back to AP mode if connection fails
  - Manages retry logic and connection status
  
- **ApiClient** (`api_client.h/cpp`): HTTPS client wrapper
  - Uses WiFiClientSecure for GitHub/Calendar API calls
  - `setInsecure()` mode available for testing (skips cert validation)

#### 4. Service Layer
- **CalendarService** (`calendar_service.h/cpp`): Fetches iCal feeds
  - Parses next event summary and time
  - Updates model.cfg.nextEventSummary/nextEventTime
  
- **GithubService** (`github_service.h/cpp`): Fetches contribution data
  - Can populate streak array from GitHub activity
  - Requires public username or API token

Both services use periodic polling (loop() method) with rate limiting.

#### 5. Input System (in `TaskyCircular.ino`)
- **Encoder**: Rotary encoder with push button
  - Detects CW/CCW rotation (±1 per detent)
  - Short click vs long press (900ms threshold)
  - Debouncing built-in
  
- **Mode Button**: Screen navigation trigger
  - Cycles: Timer → Tasks → Calendar → Streak → Stats → Settings → QR → WiFi → Timer

### Screen Flow

**Navigation Cycle**:
```
Timer → Tasks → Calendar → Streak → Stats → Settings → QR → WiFi Info → Timer
```

**Bootup Sequence**:
1. Splash screen (typewriter "Tasky" animation, 1.3s)
2. Network services initialized
3. Starts on Timer screen

### Animation System (`animation.h`)

Lightweight, framebuffer-free animation framework:
- **Enter animations**: Screens slide up with easeOut
- **Cursor smoothing**: List/grid navigation interpolation
- **Timer effects**: Snap/pulse on time change
- **Streak ink**: Drop effect when incrementing day values
- **QR breathing**: Subtle scale oscillation

Controlled via `AnimState` struct - call `triggerX()` methods to start animations, query state via `getX()` methods during rendering.

## Key Design Patterns

### Screen Rendering Pattern
All screens follow this pattern:
```cpp
void handleScreen(int step, EncClickType click) {
  if (!ui.baseDrawn) {
    ui.drawScreen();
    ui.baseDrawn = true;
  }
  // Handle input...
  // Redraw only changed elements
}
```
Set `ui.baseDrawn = false` to force full redraw.

### Data Persistence Pattern
- Load: `model.load()` called once in `setup()`
- Save: `model.save()` called after each config/task/streak change
- Preferences namespace: "tasky"

### Network Pattern
WiFi is **lazy-loaded**:
- Portal starts only when QR/WiFi Info screens are accessed
- Station mode attempts connection on boot if credentials exist
- Services poll in `loop()` only when WiFi connected

### Input Debouncing
All buttons use timestamp-based debouncing (200ms typical). Encoder has special long-press detection (900ms threshold) to distinguish reset vs pause actions.

## Important Constraints

### Memory
- ESP32-C3 has limited RAM (~400KB)
- No framebuffer - all drawing is direct to display
- Streak array limited to 84 days (6x14) vs typical 112 (7x16)
- Preferences value size limits apply to WiFi passwords and API tokens

### Hardware Quirks
- **LED + Buzzer in series**: Cannot control independently - both activate together
- **Display coordinates**: Circular mode requires corner clipping - UI must avoid edges
- **Encoder direction**: Verify CLK/DT wiring matches rotation direction expected in code

### Display Driver Selection
To switch between square (ST7789) and circular (GC9A01A) displays:
1. Edit `ui.h` line 9: `#define DISPLAY_CIRCULAR 0` (square) or `1` (circular)
2. Recompile - driver and coordinate logic will adjust automatically

## Planned Features (Not Yet Implemented)

Per `context/Conectivity_Roadmap.md`:
- Deep sleep for battery optimization
- OAuth2 flow for GitHub/Google (currently requires manual token entry)
- NTP time sync (network_manager.h has `syncTime()` stub)
- Calendar event parsing beyond "next event"

## File Structure

```
TaskyCircular/          Main firmware directory
├── TaskyCircular.ino   Entry point: setup(), loop(), screen handlers
├── model.h/cpp         Data model + persistence
├── ui.h/cpp            Display rendering
├── animation.h         Animation system (header-only)
├── network_manager.*   WiFi station mode
├── wifi_portal.*       Captive portal (AP mode)
├── api_client.*        HTTPS client wrapper
├── calendar_service.*  Calendar integration
├── github_service.*    GitHub integration
├── qrcode.h            QR code rendering
└── qrcodegen.*         QR encoding library (modified)

context/               Project documentation
├── readme.md          Hardware specs, features, pinout
├── Walktrought.md     UI/UX modernization notes
└── Conectivity_*.md   Network architecture strategy

docs/                  Adafruit library copies (reference)
```

## Coding Conventions

- **Naming**: camelCase for variables/functions, PascalCase for classes/enums
- **Screen IDs**: `SCR_TIMER`, `SCR_TASKS`, etc. (enum in ui.h)
- **Preferences keys**: Abbreviated to 3-4 chars (NVS key length limits)
- **Input**: Always read inputs once per loop, pass values to handlers
- **Timings**: Use `millis()` for all timing, never `delay()` except in setup

## Debugging Tips

- WiFi issues: Check Serial output for connection status and IP assignment
- Display glitches: Verify SPI pin definitions match hardware
- Encoder not responding: Check pullup resistors and CLK/DT pin order
- Portal not accessible: Ensure phone WiFi is connected to "Tasky" AP and browser opens 192.168.4.1
- Preferences corruption: Call `model.load()` to verify values are within expected ranges (clamping is applied)
