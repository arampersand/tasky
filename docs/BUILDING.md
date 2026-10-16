# Building Tasky from Source

You only need this guide if you want to change the firmware. To install a release, use the web flasher at **https://tasky-flash.vercel.app** (see [MANUAL.md](MANUAL.md#firmware-updates)).

| | |
| --- | --- |
| Firmware source | `firmware/tasky/` (Arduino sketch `tasky.ino` + `.cpp/.h` modules) |
| Build config | `firmware/platformio.ini` |
| Version | `firmware/tasky/version.h` → `TASKY_VERSION` |
| Core | Arduino-ESP32 **3.3.x** (via the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform) |
| Targets | `xiao_c3` (Seeed XIAO ESP32-C3), `xiao_c6` (Seeed XIAO ESP32-C6) |
| Partitions | `min_spiffs.csv`: two 1.875 MB OTA app slots (the app is about 1.5 MB) |

- [Before you start](#before-you-start)
- [PlatformIO CLI](#platformio-cli)
- [VS Code + PlatformIO](#vs-code--platformio)
- [Arduino IDE](#arduino-ide)
- [Configuration](#configuration)
- [Build outputs and flashing](#build-outputs-and-flashing)
- [Release process](#release-process)
- [Known pitfalls](#known-pitfalls)

---

## Before you start

- **Python 3.10–3.13** for PlatformIO. pioarduino fails on Python 3.14.
- **A project path without `&` or other shell-special characters.** PlatformIO's build breaks on them. See [Known pitfalls](#known-pitfalls).
- **A data-capable USB-C cable.**

```sh
git clone https://github.com/arampersand/tasky.git
cd tasky
```

---

## PlatformIO CLI

```sh
pip install -U platformio          # once

cd firmware
pio run -e xiao_c3                 # compile
pio run -e xiao_c3 -t upload       # compile and flash over USB
pio device monitor                 # serial console at 115200 baud
```

For the XIAO ESP32-C6, use `-e xiao_c6`. For the **Tasky for Agents** firmware use `-e xiao_c3_agents` / `-e xiao_c6_agents` (adds the NimBLE-Arduino library).

- **First build:** the first build downloads the pioarduino platform, the toolchain and four libraries (Adafruit GFX, Adafruit ST7735 and ST7789, Adafruit BusIO, ArduinoJson 7). This takes a few minutes.
- **Upload port:** if more than one serial device is connected, add `--upload-port /dev/cu.usbmodemXXXX` (macOS/Linux) or `--upload-port COMx` (Windows).
- **Settings:** `-t upload` writes the bootloader, partition table and app separately, so the settings stored on the device are kept.

To check that the build worked, open the serial monitor and confirm the boot log prints `Booting TaskyFirmware...` and finishes with `Setup complete.`

---

## VS Code + PlatformIO

1. Install the **PlatformIO IDE** extension.
2. Open the **`firmware/`** folder, the one that contains `platformio.ini`. Opening the repo root won't work.
3. In the PlatformIO sidebar, expand **`env:xiao_c3`** (or `env:xiao_c6`) and use **Build**, **Upload** and **Monitor**.

---

## Arduino IDE

The sketch opens directly in Arduino IDE 2.x.

1. **Install the board package.**
   1. Under **Preferences → Additional boards manager URLs**, add `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
   2. In **Boards Manager**, install **esp32 by Espressif Systems**, version **3.3.x**.
2. **Install the libraries.** In **Library Manager**, install:
   - **Adafruit GFX Library**
   - **Adafruit ST7735 and ST7789 Library**
   - **Adafruit BusIO**
   - **ArduinoJson** (by Benoit Blanchon, version 7.x)
3. **Open the sketch.** Choose **File → Open** and select `firmware/tasky/tasky.ino`. All the `.cpp/.h` files in that folder are compiled with it.
4. **Select the board.** Choose **Tools → Board → esp32 → XIAO_ESP32C3**.
5. **Select the partition scheme.** Choose **Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**.
   > [!IMPORTANT]
   > The default partition scheme has a 1.2 MB app slot, which is too small, and the build will fail with "text section exceeds available space".
6. Select the port and click **Upload**.

> [!NOTE]
> In arduino-esp32 3.3.x, the **XIAO_ESP32C6** board menu doesn't offer *Minimal SPIFFS*. For the C6, use PlatformIO (above).

---

## Configuration

All build-time options are plain `#define`s. Edit them, then rebuild.

### Secrets: `config_secrets.h` (optional)

You can bake default credentials into your own build, so a freshly flashed device comes up already configured:

```sh
cp firmware/tasky/config_secrets.h.example firmware/tasky/config_secrets.h
```

Then fill in the values you need:

| Define | Used for |
| ------ | -------- |
| `SECRET_WIFI_SSID`, `SECRET_WIFI_PASS` | WiFi |
| `SECRET_LLM_PROVIDER` (`"anthropic"` / `"openai"`), `SECRET_LLM_API_KEY` | AI agent |
| `SECRET_WEATHER_CITY`, `SECRET_WEATHER_LAT`, `SECRET_WEATHER_LON` | Weather |
| `SECRET_NOTION_API_KEY`, `SECRET_NOTION_DATABASE_ID` | Notion tasks on the Agenda screen |
| `SECRET_GITHUB_USER`, `SECRET_GITHUB_TOKEN` | GitHub streak. The token can **only** be set here |
| `SECRET_CALENDAR_URL` | Secret iCal URL |
| `SECRET_FIREBASE_*`, `SECRET_SPOTIFY_*` | Experimental features (off by default) |

- **Gitignored:** `config_secrets.h` is listed in `.gitignore`. **Never commit it.**
- **Optional:** without the file, the build still succeeds. It prints a warning and uses empty defaults.
- **Defaults only:** these values apply only when the matching setting on the device is empty. Anything you later save in the portal takes precedence.
- **Not readable from the portal:** saved secrets are never sent back to the browser; the portal only shows that a value is set.

### Feature flags: `config_features.h`

| Flag | Default | Effect |
| ---- | ------- | ------ |
| `TASKY_MODE_DEV` | `0` | `1` shows the developer fields in the portal: Notion key/database ID and the Notion/GitHub setup guides |
| `FEATURE_WEATHER` | `1` | Weather screen (Open-Meteo) |
| `FEATURE_NOTION` | `1` | Notion tasks on Agenda |
| `FEATURE_GITHUB` | `1` | GitHub activity for the streak |
| `FEATURE_CALENDAR` | `1` | iCal calendar, Agenda events, meeting alerts |
| `FEATURE_QR_CODE` | `1` | QR code on Setup (QR). With `0`, the network name and password are shown as text |
| `FEATURE_LLM` | `1` | AI agent screen |
| `FEATURE_SPOTIFY` | `0` | Experimental Spotify screen |
| `FEATURE_FIREBASE` | `0` | Experimental Firebase cloud sync |

Code for a disabled feature isn't compiled at all, which keeps the firmware smaller.

### Hardware: `config_hardware.h` and `ui.h`

- **Pins:** pin map, `ACTIVE_BUZZER`, `ROW_OFFSET` / `COL_OFFSET`, `DISPLAY_BRINGUP`. See [HARDWARE.md](HARDWARE.md).
- **Round display:** `DISPLAY_CIRCULAR` in `ui.h`.
- **Setup network credentials:** SSID `apSSID` in `wifi_portal.h` (default `Tasky`); the password is generated per device on first boot (see [CUSTOMIZING.md](CUSTOMIZING.md#changing-the-wifi-setup-network-name-and-password)).

---

## Build outputs and flashing

After every PlatformIO build, `firmware/scripts/merge_bin.py` writes the release files to `firmware/.pio/build/<env>/release/`:

| File | What it is | Flash with |
| ---- | ---------- | ---------- |
| `tasky-<env>-app.bin` | Application image only (same as `../firmware.bin`) | `pio run -t upload`, or the **OTA upload** in the device portal |
| `tasky-<env>-merged.bin` | Bootloader, partition table, `boot_app0` and the app merged into **one image for offset `0x0`** | `esptool` (full install, erases settings) |
| `tasky-<env>-{bootloader,partitions,boot_app0}.bin` + `tasky-<env>-parts.json` | The individual parts and their offsets | The web flasher (keeps settings unless you choose to erase) |

Flash a merged image with esptool:

```sh
pip install esptool
# XIAO ESP32-C3
esptool.py --chip esp32c3 write_flash 0x0 tasky-xiao_c3-merged.bin
# XIAO ESP32-C6
esptool.py --chip esp32c6 write_flash 0x0 tasky-xiao_c6-merged.bin
```

- **Bootloader mode:** if esptool can't connect, put the board into bootloader mode. Unplug USB, hold **BOOT**, plug the cable back in, then release BOOT.
- **Settings are erased:** the merged image fills the gap between its parts with `0xFF`, which overwrites the settings area (NVS). After flashing it, run the device setup again.
- **OTA needs the app-only image:** for an OTA upload through the portal, use `tasky-<env>-app.bin` (or the release's `…-ota.bin`), never the merged image. The upload is accepted only while **More → Update** is armed on the device.

---

## Release process

Releases are built and published by GitHub Actions (`.github/workflows/release.yml`).

1. **Bump the version** in `firmware/tasky/version.h`:
   ```c
   #define TASKY_VERSION "2.5.0"
   ```
2. **Commit, then tag the same version with a leading `v` and push the tag:**
   ```sh
   git commit -am "Release v2.5.0"
   git tag v2.5.0
   git push origin main v2.5.0
   ```
3. **CI builds and publishes the release:**
   - Fails the release if the tag doesn't match `version.h`.
   - Builds `xiao_c3` and `xiao_c6`.
   - Publishes a GitHub Release per board with `tasky-<board>-v2.5.0.bin` (merged, for esptool), `tasky-<board>-v2.5.0-ota.bin` (app-only, for Wi-Fi updates), the individual parts + `tasky-<board>-parts.json` (for the web flasher), `SHA256SUMS` and auto-generated release notes.
4. **The web flasher updates itself.** CI calls the `VERCEL_DEPLOY_HOOK` repository secret, which redeploys https://tasky-flash.vercel.app. The flasher pulls its firmware from the latest GitHub Release. If the secret isn't set, this step is skipped.

The separate `ci.yml` workflow builds both targets on every push to `main` and every pull request that touches `firmware/`. It uploads the merged images as build artifacts.

---

## Known pitfalls

| Symptom | Cause | Fix |
| ------- | ----- | --- |
| Build fails with odd shell or SCons errors, or files "not found" | The project path contains `&` (or another shell-special character such as `!`, `$` or `;`) | Move or clone the repo to a plain path, for example `~/src/tasky` |
| pioarduino fails to install or build | Python 3.14 | Use Python **3.10–3.13**, for example `pyenv` or `uv` with 3.12, which is what CI uses |
| `ledcAttach` was not declared | Built with the official `platformio/espressif32` platform (Arduino core 2.0.x) | Keep the pioarduino `platform =` URL from `platformio.ini` (core 3.3.x) |
| "text section exceeds available space" / sketch too big | Default partition scheme | Use `min_spiffs.csv` (PlatformIO, already set) or **Minimal SPIFFS** (Arduino IDE) |
| Warning `config_secrets.h not found` | No secrets file | Harmless. Create one only if you want built-in defaults |
| Upload can't connect | Board not in bootloader mode, or a charge-only cable | Hold **BOOT** while plugging in. Try another cable |
