# Make your own Tasky

Tasky is meant to be forked. This guide covers the changes people make most often, from turning features on and off to shipping a fork with its own browser flasher. Each section names the file and the exact symbol you edit.

Before you start, set up the toolchain and make sure a stock build works: see [BUILDING.md](BUILDING.md). For how the pieces fit together, see [ARCHITECTURE.md](ARCHITECTURE.md).

All firmware paths below are relative to `firmware/tasky/`.

## Contents

- [Feature flags](#feature-flags)
- [User mode vs dev mode](#user-mode-vs-dev-mode)
- [Compile-time secrets](#compile-time-secrets)
- [Switching to a round display (GC9A01A)](#switching-to-a-round-display-gc9a01a)
- [Remapping pins](#remapping-pins)
- [Fixing display offsets](#fixing-display-offsets)
- [Theme colours](#theme-colours)
- [Changing texts](#changing-texts)
- [Changing the WiFi setup network name and password](#changing-the-wifi-setup-network-name-and-password)
- [Timer defaults](#timer-defaults)
- [Adding a new screen](#adding-a-new-screen)
- [Adding a new service or integration](#adding-a-new-service-or-integration)
- [Publishing your own fork with its own web flasher](#publishing-your-own-fork-with-its-own-web-flasher)
- [License obligations](#license-obligations)

---

## Feature flags

`config_features.h` decides which optional modules get compiled. A flag set to `0` removes the module's code, its screen, its More menu entry and its portal fields from the build, so the flash space really comes back.

```cpp
#define FEATURE_WEATHER  1   // Clima - contexto del dia
#define FEATURE_SPOTIFY  0   // Spotify - disabled until BLE music from phone app is ready
#define FEATURE_FIREBASE 0   // Cloud sync - opcional (sync entre dispositivos)
#define FEATURE_NOTION   1   // Notion - tareas en Agenda
#define FEATURE_GITHUB   1   // GitHub - streak para desarrolladores
#define FEATURE_CALENDAR 1   // iCal + Agenda - tu calendario
#define FEATURE_QR_CODE  1   // QR para WiFi setup
#define FEATURE_LLM      1   // AI Agent - cloud LLM integration (Claude/OpenAI)
```

| Flag | Default | What it adds | Files it gates |
|---|---|---|---|
| `FEATURE_WEATHER` | 1 | Current weather from Open-Meteo (no API key). Adds the **Weather** screen in More and the temperature on the idle (Ambient) screen. | `weather_service.*`, `drawWeather()` |
| `FEATURE_CALENDAR` | 1 | Fetches a secret iCal URL every 15 min. Feeds **Agenda**, **Calendar**, meeting alerts and the morning brief. | `calendar_service.*` |
| `FEATURE_NOTION` | 1 | Reads tasks from a Notion database every 10 min and shows them in **Agenda**. Its portal fields only appear in dev mode. | `notion_service.*` |
| `FEATURE_GITHUB` | 1 | Reads your GitHub contribution count every 30 min for the streak. | `github_service.*` |
| `FEATURE_LLM` | 1 | **Agent** screen: one short daily insight from Anthropic or OpenAI, with built-in rate limits (20 requests per hour, 100 per day). | `llm_service.*`, `drawAgent*()` |
| `FEATURE_QR_CODE` | 1 | Draws a QR code on the **Setup (QR)** screen so a phone can join the setup network. | `qrcode.h`, `qrcodegen.*` |
| `FEATURE_SPOTIFY` | 0 | Experimental now-playing screen with play/pause, next and volume controls. Off until music control from the phone is ready. | `spotify_service.*`, `drawSpotify()` |
| `FEATURE_FIREBASE` | 0 | Experimental cloud sync to a Firebase Realtime Database every 15 min. | `firebase_service.*` |

### Flash size impact

A default build is about **1.61 MB**, and the app slot holds **1.875 MB** (see [ARCHITECTURE.md](ARCHITECTURE.md#memory-and-flash-budget)). The modules that pull in HTTPS and do the most text parsing take the most space. In order of code size, those are `FEATURE_LLM`, `FEATURE_CALENDAR` and `FEATURE_SPOTIFY`, followed by the smaller REST clients. Exact savings depend on the toolchain version, so measure them yourself:

```bash
cd firmware
pio run -e xiao_c3                                # note "Flash: [=====  ] xx.x% (used N bytes ...)"
# set one flag to 0 in tasky/config_features.h, then:
pio run -e xiao_c3
ls -l .pio/build/xiao_c3/firmware.bin             # app image size (this is what must fit in the OTA slot)
```

> [!IMPORTANT]
> If you turn on several flags and the build approaches 1.875 MB, the link step fails with `section .flash.text will not fit` or the size check reports more than 100%. Turn something off; do not shrink the OTA slots. Over-the-air updates need two slots.

---

## User mode vs dev mode

`TASKY_MODE_DEV` in `config_features.h` switches between two portal and UI variants:

```cpp
#define TASKY_MODE_DEV 0   // 0 = user mode, 1 = dev mode
```

| | User mode (`0`, default) | Dev mode (`1`) |
|---|---|---|
| Setup portal | Simplified: WiFi, timezone, tasks, timer, workday, GitHub user, calendar URL, streak source, weather, AI key | Adds a "developer mode" banner, the Notion setup guide and API key/database fields, and the GitHub setup guide |
| Empty Weather screen | Clock plus a motivational line | Technical "not configured" state |
| Empty Spotify screen | Friendly "Play music" hint | "No active playback" diagnostics |

Every use is a plain `#if TASKY_MODE_DEV` or `#if FEATURE_X && TASKY_MODE_DEV` in `wifi_portal.cpp` and `ui.cpp`. Search for `TASKY_MODE_DEV` to find them all. Release builds ship with `0`.

---

## Compile-time secrets

For your own unit, you can bake in WiFi credentials and API keys so you don't have to type them into the portal:

```bash
cp firmware/tasky/config_secrets.h.example firmware/tasky/config_secrets.h
```

`config_secrets.h` is gitignored. If it is missing, the build still works: `config_features.h` defines empty defaults and prints a `#warning`. Values from it are only used when the matching NVS setting is empty (see `TaskyModel::load()` in `model.cpp`). Anything saved from the portal therefore wins.

> [!CAUTION]
> Never commit `config_secrets.h`, and never publish a binary built with your secrets in it. Anyone can extract strings from a `.bin`.

---

## Switching to a round display (GC9A01A)

The UI is designed for a **square 240x240 ST7789**. You can also compile it for a 240x240 round GC9A01A panel:

1. In `ui.h`, set the switch:

   ```cpp
   #define DISPLAY_CIRCULAR 1 // 0 = ST7789 square, 1 = GC9A01A circular
   ```

2. Add the driver to `lib_deps` in `firmware/platformio.ini`. It is not listed by default:

   ```ini
   lib_deps =
       adafruit/Adafruit GFX Library
       adafruit/Adafruit ST7735 and ST7789 Library
       adafruit/Adafruit GC9A01A
       adafruit/Adafruit BusIO
   ```

3. Keep `DISPLAY_BRINGUP 0` in `config_hardware.h`. The bring-up scanner uses the ST7789 subclass only.

What changes in code: `TaskyUI::tft` becomes an `Adafruit_GC9A01A`, and `TaskyUI::init()` calls `tft.begin()`, `setRotation(0)` and `invertDisplay(true)`. The ST7789 offset call is skipped.

Layouts are not redrawn for a circle. Corners are cut off, for example the timer's rounded-rectangle progress stroke and the list rows. Expect to adjust coordinates in `ui.cpp`.

---

## Remapping pins

All pins are in `config_hardware.h`, written as XIAO `Dn` names so the same file works on the C3 and the C6:

```cpp
// SPI DISPLAY
#define TFT_CS D1
#define TFT_DC D2
#define TFT_RST D3
#define TFT_SCK D8
#define TFT_MOSI D10
#define TFT_MISO D9  // Not used (But reserved)

// INPUTS
#define ENCODER_CLK D4
#define ENCODER_DT D5
#define ENCODER_SW D6
#define MODO_BTN D7

// PERIPHERALS
#define LED_PIN D9
#define ACTIVE_BUZZER 1 // 1 = Active Buzzer (High/Low), 0 = Passive (PWM)
#define BUZZER_PIN D0
#define TFT_BL D0
```

Things to know before you move a wire:

- **Inputs are active-low** with internal pull-ups (`INPUT_PULLUP` in `setup()`). Wire buttons and the encoder switch to GND.
- **Encoder direction reversed?** Swap `ENCODER_CLK` and `ENCODER_DT`.
- **`TFT_BL` and `BUZZER_PIN` share D0** on the reference board. `buzzerOff()` restores the backlight after every beep. If you give the backlight its own pin, keep the define. To have no backlight control at all, set `TFT_BL` to `-1`, and the `#if TFT_BL >= 0` blocks compile out.
- **`LED_PIN` reuses D9 (SPI MISO).** That is fine because the display is write-only. The light bar is driven with LEDC PWM (`ledcAttach(LED_PIN, 5000, 8)`), so any output-capable pin works.
- **Passive buzzer:** set `ACTIVE_BUZZER 0`. `buzzerTone()` then calls `ledcWriteTone()`, and you must also attach LEDC to `BUZZER_PIN` in `setup()`. There is a commented `ledcAttach(BUZZER_PIN, 2000, 8)` line next to the LED one.
- On the C3, D0, D8 and D9 are strapping pins (GPIO 2, 8 and 9). Do not attach anything that holds them low during reset, or the chip may not boot.

Full wiring tables are in [HARDWARE.md](HARDWARE.md).

---

## Fixing display offsets

ST7789 controllers have 240x320 of RAM, and smaller panels map to a window inside it. If the image is shifted, wrapped, or shows a band of noise at one edge, the window is wrong.

**Step 1: set the real panel size** in `config_hardware.h`:

```cpp
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240   // 240 = square 240x240 | 280 = 240x280
```

`TaskyUI::init()` passes these to `tft.init(SCREEN_WIDTH, SCREEN_HEIGHT)`, and the Adafruit driver picks the standard window for that size. For 240x240 that is rows 80 to 319 at rotation 0, and rows 0 to 239 at rotation 2, which is the rotation Tasky uses. For 240x280 it is a 20-row centred window. Most panels are fixed by this step alone.

**Step 2, only for unusual panels: a custom offset.** `config_hardware.h` also has:

```cpp
#define ROW_OFFSET 0
#define COL_OFFSET 0
```

> [!WARNING]
> In the current code these two values have **no visible effect**. `init()` calls `tft.setOffsets(COL_OFFSET, ROW_OFFSET)` *after* `tft.setRotation(2)`. The driver copies offsets into its address window only inside `setRotation()`, and rotation 2 reads a second pair of offsets (`_colstart2`/`_rowstart2`) that `setOffsets()` does not touch.

To make a custom offset work, extend `TaskyST7789` in `ui.h`:

```cpp
  // Set the offsets every rotation uses; call setRotation() afterwards to apply
  void setAllOffsets(int8_t col, int8_t row) {
    setColRowStart(col, row);
    _colstart2 = col;
    _rowstart2 = row;
  }
```

Then change the order in `TaskyUI::init()`:

```cpp
  tft.init(SCREEN_WIDTH, SCREEN_HEIGHT);
  tft.setSPISpeed(10000000);
  tft.setAllOffsets(COL_OFFSET, ROW_OFFSET);  // before setRotation
  tft.setRotation(2);
  tft.invertDisplay(true);
```

Once you make this change, `ROW_OFFSET`/`COL_OFFSET` replace the driver's defaults completely. A stock 240x240 panel then needs `0, 0`, and a 240x280 panel needs `ROW_OFFSET 20`.

**Display bring-up mode.** Setting `DISPLAY_BRINGUP 1` in `config_hardware.h` runs a test loop instead of Tasky. It fills the screen red, green and blue, flips colour inversion, and prints each step on the serial monitor (115200 baud). Use it to confirm wiring, SPI and inversion. It calls `setOffsets()` after `setRotation()` too, so its "OFF: 20 / 80" steps do not actually move the image. Set it back to `0` when you are done.

> [!NOTE]
> `SCREEN_HEIGHT 280` only tells the driver the panel size. The layout in `ui.cpp` uses fixed `W = 240, H = 240` constants, so on a 240x280 panel the UI fills the top 240 rows. Adjust `H`/`CY` and the layouts if you want to use the extra 40 px.

---

## Theme colours

Colours are 16-bit **RGB565** values (5 bits red, 6 bits green, 5 bits blue). That is the native format of the display.

The accent colours are members of `TaskyUI` in `ui.h`:

```cpp
uint16_t C_TEAL = 0x2E57;    // work phase accent, selection highlight, splash text
uint16_t C_GREEN = 0x07E0;
uint16_t C_ORANGE = 0xFD20;
uint16_t C_PURPLE = 0x780F;  // break phase accent
uint16_t C_RED = 0xF800;
```

The light/dark base palette is set in `TaskyUI::applyTheme()` in `ui.cpp`:

```cpp
if (cfg.darkMode) {
  C_BG = 0x0000;  C_TEXT = 0xFFFF;  C_MUTED = 0x7BEF;
  C_LINE = 0x3186; C_SOFT = 0xE71C; C_SPLASH_BG = 0x0000;
} else {
  C_BG = 0xFFFF;  C_TEXT = 0x0000;  C_MUTED = 0x7BEF;
  C_LINE = 0xC618; C_SOFT = 0x39E7; C_SPLASH_BG = 0xFFFF;
}
```

A few colours are hardcoded where they are used. Search `ui.cpp` for `0x` to find them, for example `getRingColor()`, the weather icons (`0xFFE0` yellow) and the QR screen.

### Converting colours

From `#RRGGBB` to RGB565:

```
rgb565 = ((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3)
```

From RGB565 back to `#RRGGBB` (expand each field to 8 bits):

```
r5 = (c >> 11) & 0x1F;  g6 = (c >> 5) & 0x3F;  b5 = c & 0x1F
R = (r5 << 3) | (r5 >> 2);  G = (g6 << 2) | (g6 >> 4);  B = (b5 << 3) | (b5 >> 2)
```

Worked example with Tasky teal `0x2E57`: r5 = 5, g6 = 50, b5 = 23, which gives **`#29CBBD`**. The web flasher and the 3D device model use `#29CEBD`, which is the same teal to the eye. It converts back to `0x2E77`, one green step away, because RGB565 cannot represent every 24-bit colour.

A one-liner to convert a hex colour:

```bash
python3 -c "c=0x29CEBD; print(hex(((c>>19)&0x1F)<<11 | ((c>>10)&0x3F)<<5 | ((c>>3)&0x1F)))"
```

This prints `0x2e77`. Replace `0x29CEBD` with your own `#RRGGBB` written as `0x......`.

The default theme is dark (`darkMode = true` in `Config`, `model.h`). Users can toggle it on the device under **More > Settings**.

---

## Changing texts

There is no string table. Texts are literals next to the code that draws them:

| What | Where |
|---|---|
| Screen titles and labels ("STATS", "MORE", "Total Focus Time", ...) | `centerTextFont(...)` / `tft.print(...)` calls in `ui.cpp` |
| More menu entries | `MORE_ITEMS[]` in `tasky.ino` |
| Timer finish message ("WELL DONE") and focus-lock toast ("Pause to unlock") | `handleTimer()` and `loop()` in `tasky.ino` |
| Setup portal page (currently mostly Spanish) | `WiFiPortal::htmlPage()` in `wifi_portal.cpp` |
| Default task name `"Empty"` | `TaskyModel::load()` in `model.cpp` and `handleSave()` in `wifi_portal.cpp` |
| AI agent mode names and prompts | `llm_service.cpp` |

Fonts are Adafruit GFX FreeSans fonts (`ui.h`). They only contain ASCII, so accented characters will not render. Use `fitText()` in `ui.cpp` when a string might be wider than its slot.

---

## Changing the WiFi setup network name and password

The setup network (soft AP plus captive portal) is defined in `wifi_portal.h`:

```cpp
String apSSID = "Tasky";
String apPass = "GetItDone";
```

Rules:

- The WPA2 password must be **8 to 63 characters**. A shorter one makes `WiFi.softAP()` fail.
- The Setup (QR) and Update screens read `portal.apSSID` / `portal.apPass`, so the QR code and the on-screen hints update automatically.
- If you sell or give away devices, consider a per-device password (for example, derived from the MAC address) instead of a shared one.

---

## Timer defaults

The default durations are in **two** places, and both must change:

1. The `Config` struct in `model.h`:

   ```cpp
   uint8_t workMin = 25;
   uint8_t breakMin = 5;
   ```

2. The NVS fallbacks in `TaskyModel::load()` in `model.cpp`:

   ```cpp
   cfg.workMin = pref.getUChar("work", 25);
   cfg.breakMin = pref.getUChar("break", 5);
   ```

Defaults only apply when nothing is stored yet, that is on a fresh flash with **Erase device** or after a factory reset (hold both buttons for 3 s).

Allowed ranges and step sizes are enforced in several places. Keep them consistent if you change them:

| Place | Work | Break | Step |
|---|---|---|---|
| `handleTimer()` in `tasky.ino` (turning the encoder on the Timer) | 5 to 90 | 1 to 30 | 5 min |
| `handleSettings()` in `tasky.ino` | 5 to 90 | 1 to 30 | 5 min |
| `TaskyModel::load()` clamp | 5 to 90 | 5 to 90 | n/a |
| `handleSave()` in `wifi_portal.cpp` (portal) | 5 to 90 | n/a | n/a |

Other timing constants are at the top of `tasky.ino`: `AMBIENT_IDLE_MS` (5 min until the idle screen), `MEETING_LEAD_MIN` (5 min meeting alert lead time), `ENC_LONGPRESS_MS` (900 ms) and `BRIEF_BOOT_DELAY_MS`.

---

## Adding a new screen

This walk-through adds a **Hello** screen to the More menu. It shows a counter: turning the encoder changes it, and a click resets it. Every screen follows the same three-part pattern: an enum value, a draw method, and a handler that is called on every loop.

### 1. Add the screen to the enum (`ui.h`)

Add it with the other screens that open from More, before the overlays:

```cpp
enum Screen {
  // ...
  SCR_QR,
  SCR_WIFI_INFO,
  SCR_HELLO,      // <-- new
  // Overlays
  SCR_AMBIENT,
  SCR_NOTICE
};
```

### 2. Declare and implement the draw method

In `ui.h`, add a public method to `TaskyUI`:

```cpp
  void drawHello(int count);
```

In `ui.cpp`, implement it using the shared palette and helpers. `centerTextFont()` is private to `TaskyUI`, so draw methods can call it:

```cpp
// ==================== HELLO ====================
void TaskyUI::drawHello(int count) {
  tft.fillScreen(C_BG);
  centerTextFont("HELLO", 34, C_TEXT, &FreeSansBold12pt7b);
  tft.drawFastHLine(40, 46, 160, C_LINE);

  char buf[12];
  snprintf(buf, sizeof(buf), "%d", count);
  centerTextFont(buf, 130, C_TEAL, &FreeSansBold24pt7b);
  centerTextFont("Turn to count, click to reset", 200, C_MUTED, &FreeSans9pt7b);
}
```

Keep all content inside the `SAFE` margin (12 px): the case window hides the panel edge.

### 3. Write the handler (`tasky.ino`)

Handlers take the step and click that `loop()` read **once** this iteration. They draw the full screen only when `ui.baseDrawn` is false. `goTo()` clears that flag on every navigation, so the first loop on a new screen always paints it:

```cpp
void handleHello(int step, EncClickType click) {
  static int count = 0;
  if (!ui.baseDrawn) {
    ui.drawHello(count);
    ui.baseDrawn = true;
  }
  if (step != 0) {
    count += step;
    ui.uiTick(false);
    ui.drawHello(count);
  }
  if (click == ENC_SHORT) {
    ui.uiTick(true);
    count = 0;
    ui.drawHello(count);
  }
}
```

Never call `delay()` in a handler. For something that animates, keep a `static uint32_t lastAt` and redraw when `millis() - lastAt` exceeds your frame time (see the 1-second countdown in `handleUpdate()`). For screens with a lot of content, redraw only the part that changed instead of calling `fillScreen()` each time (see `drawTimerTime()`).

### 4. Dispatch it from `loop()`

Add a case to the `switch (screen)` at the bottom of `loop()`:

```cpp
  case SCR_HELLO:
    handleHello(step, click);
    break;
```

### 5. Make it reachable

Navigation is table-driven in `tasky.ino`. There are two tables:

- **More menu** (recommended for new screens). Add an entry to `MORE_ITEMS[]`. The labels are copied into `moreLabels[]` in `setup()`, and `drawMore()` scrolls automatically, so nothing else changes:

  ```cpp
  static const MoreItem MORE_ITEMS[] = {
      {"Focus", SCR_FOCUS},
      {"Calendar", SCR_CALENDAR},
      {"Streak", SCR_STREAK},
      // ...
      {"Hello", SCR_HELLO},          // <-- new
      {"Update", SCR_UPDATE},
      {"Setup (QR)", SCR_QR},
      {"Settings", SCR_SETTINGS},
  };
  ```

  Pressing MODO on any screen that is not in the carousel returns to More (`navigateNext()` / `navigatePrev()`).

- **Main carousel** (MODO cycles through it, and a double press goes back). Add the screen to `CAROUSEL[]`. Keep this list short, because every entry adds a press to the everyday loop:

  ```cpp
  static const Screen CAROUSEL[] = {SCR_TIMER, SCR_TASKS, SCR_AGENDA, SCR_STATS,
                                    SCR_MORE};
  ```

If your screen depends on an optional module, wrap the enum use, the handler, the `case` and the `MORE_ITEMS` entry in `#if FEATURE_X ... #endif`, as Weather and Agent do.

If the screen needs the setup network running (it shows the portal URL, for example), add it to the check at the top of `goTo()`, which calls `portal.begin()` for `SCR_QR`, `SCR_WIFI_INFO` and `SCR_UPDATE`. If the screen should never be interrupted by the idle screen, add it to `idleAllowed` in `loop()`.

### 6. Build both boards

```bash
cd firmware && pio run -e xiao_c3 && pio run -e xiao_c6
```

---

## Adding a new service or integration

Services are small classes that fetch data from the network on their own schedule and store it on the model or on themselves. `weather_service.*` is the reference implementation. This walk-through adds an **air quality** service using Open-Meteo's free air-quality API, reusing the weather location.

### The pattern

| Part | Convention |
|---|---|
| Constructor | Takes `TaskyModel *`. Configures its `ApiClient` (call `api.setInsecure()` for HTTPS, as all existing services do). |
| `begin()` | Called once in `setup()`. Cheap: no blocking network calls unless already configured. |
| `loop()` | Called every **5 s** from `loop()`. Returns immediately unless configured *and* its own interval has elapsed. |
| `forceUpdate()` | Does the actual fetch. Used by `loop()` and by "click to refresh" on screens. |
| Rate limiting | `lastUpdate == 0 ? 10000 : INTERVAL`: first try 10 s after boot (WiFi needs time), then at a long interval. On failure, retry sooner rather than hammering. |
| Network | `ApiClient::get()` returns `""` when WiFi is down or the request fails, so always check for an empty response. Calls are **blocking**, so keep responses small (ask the API for only the fields you need) and intervals long. |
| Guard | Header and `.cpp` body wrapped in `#if FEATURE_X`. Every use in `tasky.ino` is wrapped too. |
| Settings | New `Config` fields in `model.h`, persisted in `load()`/`save()` with NVS keys of **15 characters or fewer**. |
| Portal | Inputs in `WiFiPortal::htmlPage()` and parsing in `WiFiPortal::handleSave()`, both under `#if FEATURE_X`. |

### 1. Add the flag (`config_features.h`)

```cpp
#define FEATURE_AIR      1   // Air quality (Open-Meteo, uses weather location)
```

### 2. Header (`air_service.h`)

```cpp
#pragma once

#include "api_client.h"
#include "model.h"
#include <Arduino.h>

class AirService {
public:
  AirService(TaskyModel *model);

  void begin();
  void loop();
  void forceUpdate();

  bool hasData() const { return dataValid; }
  int aqi = -1;  // European AQI

private:
  TaskyModel *model;
  ApiClient api;
  uint32_t lastUpdate = 0;
  bool dataValid = false;

  static const uint32_t UPDATE_INTERVAL_MS = 60UL * 60UL * 1000UL; // 1 hour
};
```

### 3. Implementation (`air_service.cpp`)

```cpp
#include "config_features.h"
#if FEATURE_AIR
#include "air_service.h"

AirService::AirService(TaskyModel *m) : model(m) { api.setInsecure(); }

void AirService::begin() {}

void AirService::loop() {
  Config &c = model->cfg;
  if (!c.airOn || (c.weatherLat == 0 && c.weatherLon == 0))
    return;
  uint32_t interval = (lastUpdate == 0) ? 10000 : UPDATE_INTERVAL_MS;
  if (millis() - lastUpdate > interval)
    forceUpdate();
}

void AirService::forceUpdate() {
  String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=";
  url += String(model->cfg.weatherLat, 4);
  url += "&longitude=";
  url += String(model->cfg.weatherLon, 4);
  url += "&current=european_aqi";

  String json = api.get(url);
  if (json.length() == 0) {
    Serial.println("[Air] Fetch failed - retry in 60s");
    lastUpdate = millis() - UPDATE_INTERVAL_MS + 60000;
    return;
  }

  int cur = json.indexOf("\"current\":{");          // skip "current_units"
  int k = (cur < 0) ? -1 : json.indexOf("\"european_aqi\":", cur);
  if (k < 0) {
    dataValid = false;
  } else {
    aqi = json.substring(k + 15).toInt();
    dataValid = true;
  }
  lastUpdate = millis();
}
#endif
```

The existing services parse JSON with `indexOf()`/`substring()` instead of a JSON library to save flash. Follow that approach for small payloads.

### 4. Model fields and persistence (`model.h`, `model.cpp`)

```cpp
// model.h, inside struct Config
  bool airOn = false;               // Air quality enabled
```

```cpp
// model.cpp, TaskyModel::load()
  cfg.airOn = pref.getBool("airOn", false);

// model.cpp, TaskyModel::save()
  pref.putBool("airOn", cfg.airOn);
```

NVS keys are limited to **15 characters**. Existing keys are deliberately terse (`"wCity"`, `"fbKey"`, `"lMod"`). Check the key table in [ARCHITECTURE.md](ARCHITECTURE.md#persistence) so you don't collide with an existing one. Keep the `Config` fields unconditional, as the existing ones are: `model.*` does not depend on feature flags, so NVS layouts stay stable across builds.

### 5. Wire it into `tasky.ino`

```cpp
#if FEATURE_AIR
#include "air_service.h"
#endif
// ...
#if FEATURE_AIR
AirService air(&model);
#endif

// setup(), next to the other begin() calls
#if FEATURE_AIR
  air.begin();
#endif

// loop(), inside the 5-second "lastServiceCheck" block
#if FEATURE_AIR
    air.loop();
#endif
```

To show the value, either add a screen (see [Adding a new screen](#adding-a-new-screen)) or pass `air.aqi` into an existing draw call, the way `drawAmbientScreen()` passes weather data.

### 6. Portal field (`wifi_portal.cpp`)

In `WiFiPortal::htmlPage()`, add a section next to the weather one:

```cpp
#if FEATURE_AIR
  s += "<hr><h3>Air quality</h3>";
  s += "<div class='section'>";
  s += "<label>Show air quality (uses the weather location)</label>";
  s += "<select name='airOn'>";
  s += model->cfg.airOn ?
       "<option value='1' selected>ON</option><option value='0'>OFF</option>" :
       "<option value='1'>ON</option><option value='0' selected>OFF</option>";
  s += "</select>";
  s += "</div>";
#endif
```

In `WiFiPortal::handleSave()`, before `model->save()`:

```cpp
#if FEATURE_AIR
  if (server.hasArg("airOn"))
    model->cfg.airOn = server.arg("airOn") == "1";
#endif
```

If the integration needs a secret (API key or token), also add a `SECRET_...` define to `config_secrets.h.example`, a fallback to the `#else` block in `config_features.h`, and an "apply when empty" line at the end of `TaskyModel::load()`. Use `type='password'` for the portal input.

### 7. Build both boards and check the size

```bash
cd firmware && pio run -e xiao_c3 && pio run -e xiao_c6
```

---

## Publishing your own fork with its own web flasher

Releases are fully automated. You tag, GitHub Actions builds both boards and publishes a Release, and a Vercel deploy hook rebuilds the flasher site, which downloads the newest Release at build time.

```
git tag vX.Y.Z ──▶ release.yml builds xiao_c3 + xiao_c6 ──▶ GitHub Release
                                                               │ (deploy hook)
                     your-flasher.vercel.app ◀── web/ build fetches latest Release
```

### One-time setup

1. **Fork** the repository on GitHub and clone your fork.
2. **Enable Actions** on the fork (GitHub disables workflows on new forks: open the **Actions** tab and confirm).
3. **Create the Vercel project.** On vercel.com: **Add New > Project**, import your fork, then set:
   - **Root Directory:** `web`
   - **Framework Preset:** Other (`web/vercel.json` already sets `buildCommand: node scripts/fetch-firmware.mjs` and `outputDirectory: public`)
   - **Environment Variables:**
     - `TASKY_REPO` = `<your-user>/<your-repo>` (required, otherwise the site serves upstream's firmware)
     - `GITHUB_TOKEN` = a fine-grained read-only token (optional; avoids GitHub API rate limits during builds)
4. **Create a Deploy Hook.** In Vercel go to **Project > Settings > Git > Deploy Hooks**, create one for branch `main`, and copy the URL.
5. **Store the hook as a repository secret:**

   ```bash
   gh secret set VERCEL_DEPLOY_HOOK --repo <your-user>/<your-repo>
   # paste the hook URL when prompted
   ```

   Without this secret, releases still publish, and the redeploy step is skipped with a log message.

### Every release

1. Make your changes and build both boards locally.
2. Bump the version in `firmware/tasky/version.h`. The release workflow **refuses a tag that does not match this string**:

   ```cpp
   #define TASKY_VERSION "2.5.0"
   ```

3. Commit, tag and push:

   ```bash
   git commit -am "chore: release v2.5.0"
   git push
   git tag v2.5.0 && git push --tags
   ```

4. Watch the run (`gh run watch`). When it finishes, the Release contains:
   - `tasky-<board>-v2.5.0.bin`: merged image for `esptool` at offset `0x0`
   - `tasky-<board>-v2.5.0-ota.bin`: app-only image for Wi-Fi updates
   - `tasky-<board>-{bootloader,partitions,boot_app0,app}.bin` + `tasky-<board>-parts.json`: individual parts, used by the web flasher
   - `SHA256SUMS`

5. The deploy hook rebuilds your flasher. Check that `https://<your-project>.vercel.app/firmware/latest.json` shows the new version.

> [!NOTE]
> For updates over WiFi through the device's **More > Update** page, upload the **app-only** image (`…-ota.bin` from the release, or `firmware/.pio/build/<env>/release/tasky-<env>-app.bin` from a local build), not the merged image.

To test the flasher locally before publishing:

```bash
cd web
TASKY_REPO=<your-user>/<your-repo> node scripts/fetch-firmware.mjs
npx serve public     # Web Serial needs Chrome/Edge; localhost counts as secure
```

---

## License obligations

Tasky is **GPL-3.0**. If you distribute a fork, whether as a binary, a web flasher or pre-flashed devices:

- your fork must stay under GPL-3.0;
- you must offer the complete corresponding source code (a public repository is the easy way);
- keep the copyright and license notices, and state that you changed the code.

See [`LICENSE`](../LICENSE) for the full terms.
