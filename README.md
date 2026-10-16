<div align="center">

<img src="docs/images/tasky-focus.jpg" alt="Tasky running a 25-minute focus session" width="420">

# Tasky

**A tiny open-source desk companion that keeps you in focus.**<br>
A knob, a screen and a light bar: focus timer, today's tasks, your streak, your next meeting and an AI agent, without reaching for your phone.

[![ci](https://github.com/arampersand/tasky/actions/workflows/ci.yml/badge.svg)](https://github.com/arampersand/tasky/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/arampersand/tasky?color=29cebd&label=firmware)](https://github.com/arampersand/tasky/releases/latest)
[![license](https://img.shields.io/badge/license-GPL--3.0-3f7dff)](LICENSE)
[![board](https://img.shields.io/badge/XIAO-ESP32--C3%20%7C%20C6-555)](docs/HARDWARE.md)

### [⚡ Flash it from your browser →](https://tasky-flash.vercel.app)

[Manual](docs/MANUAL.md) · [Hardware](docs/HARDWARE.md) · [Build](docs/BUILDING.md) · [Make your own](docs/CUSTOMIZING.md) · [Architecture](docs/ARCHITECTURE.md) · [Roadmap](docs/ROADMAP.md) · [Español](README.es.md)

</div>

---

<table>
<tr>
<td><img src="docs/images/tasky-front.jpg" alt="Tasky front" width="200"></td>
<td><img src="docs/images/tasky-tasks.jpg" alt="Tasks screen" width="200"></td>
<td><img src="docs/images/tasky-stats.jpg" alt="Stats screen" width="200"></td>
<td><img src="docs/images/tasky-menu.jpg" alt="More menu" width="200"></td>
</tr>
</table>

## Why Tasky

Your phone is the worst focus tool you own. Tasky is a single-purpose object that sits next to your keyboard. Turn the knob, click, and you're working. The light bar tells the people around you you're heads-down. Everything is open: firmware, docs and the web flasher, so you can build one from off-the-shelf parts and make it yours.

## Features

| | |
|---|---|
| ⏱ **Focus timer** | Pomodoro-style sessions with a draining progress ring, session dots and an alarm. Focus mode turns the light bar on and locks the MODE button so you stay on the timer. |
| ✅ **Tasks** | Three tasks for today. Focus minutes are tracked per task; finishing tasks feeds your streak. |
| 🔥 **Streak** | GitHub-style contribution grid fed by focus sessions and, optionally, your real GitHub contributions. |
| 📅 **Calendar & agenda** | Any iCal feed (Google, Outlook, iCloud). Meeting alert 5 minutes before, a morning brief and an end-of-day close. |
| 🌤 **Weather** | Open-Meteo, so no API key is needed. Just your city. |
| 🤖 **AI agent** | Bring your Claude or OpenAI key. |
| 📊 **Stats** | Total focus time, sessions and minutes today. |
| 📶 **Wi-Fi setup portal** | Tasky creates its own network with a captive portal and a QR code. Configure everything from your phone. |
| 🔄 **Updates** | Flash from the browser, or OTA over Wi-Fi (armed from the device itself for safety). |

## Flash in 60 seconds

1. Open **[tasky-flash.vercel.app](https://tasky-flash.vercel.app)** in Chrome or Edge on a computer.
2. Pick **XIAO ESP32-C3** or **C6** and plug the board in with a USB-C *data* cable. If no port shows up, hold `BOOT` while plugging in.
3. Click **Connect & flash**.
4. On Tasky, double-press **MODE** → **More** → **Setup (QR)**. Scan the QR (or join Wi-Fi **`Tasky`** with the password shown on screen; every Tasky has its own) and the setup page opens (or go to `192.168.4.1`).

The page always serves the newest [release](https://github.com/arampersand/tasky/releases/latest) and keeps your settings when you reinstall. Prefer the command line? (full image, erases settings)

```bash
esptool.py --chip esp32c3 write_flash 0x0 tasky-xiao_c3-vX.Y.Z.bin
```

Full walkthrough: **[docs/MANUAL.md](docs/MANUAL.md#quick-start)**.

## Build one

| Part | Notes |
|---|---|
| Seeed Studio **XIAO ESP32-C3** or **ESP32-C6** | Same pinout, same firmware |
| **1.54" 240×240 ST7789** SPI IPS display | Round GC9A01A also supported |
| **Rotary encoder** with push switch | EC11-style |
| Tactile button | MODE / navigation |
| Active buzzer + LED(s) | Wired in series, light bar |
| 3D-printed enclosure | |

Pinout, wiring and variants: **[docs/HARDWARE.md](docs/HARDWARE.md)**. Building from source with PlatformIO or the Arduino IDE: **[docs/BUILDING.md](docs/BUILDING.md)**.

```bash
git clone https://github.com/arampersand/tasky && cd tasky/firmware
pio run -e xiao_c3 -t upload && pio device monitor
```

## Make your own Tasky

Tasky is meant to be forked. **[docs/CUSTOMIZING.md](docs/CUSTOMIZING.md)** walks through:

- turning features on and off (`firmware/tasky/config_features.h`) to fit flash or taste
- swapping to a round display, remapping pins, fixing panel offsets
- changing colours, texts and the Wi-Fi network name
- **adding your own screen** and **your own integration**, step by step
- **publishing your fork with its own one-click web flasher**: tag a release, import `web/` into Vercel, done

## How it fits together

```mermaid
flowchart LR
  A[git tag vX.Y.Z] --> B[GitHub Actions<br/>build C3 + C6]
  B --> C[GitHub Release<br/>images + SHA256SUMS]
  B -- deploy hook --> D[Vercel build<br/>fetch-firmware.mjs]
  C --> D
  D --> E[tasky-flash.vercel.app<br/>ESP Web Tools]
  E -- Web Serial --> F[Your Tasky]
```

```
firmware/   PlatformIO project: tasky/ sketch (UI, model, services, portal, OTA)
web/        Static landing + browser flasher (Vercel), 3D <tasky-device> model
docs/       Manual, hardware, building, customizing, architecture, improvements
```

Deep dive: **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)**.

## Roadmap

Next up is **Tasky 3.0**: set up entirely from the browser (Wi-Fi → calendar → GitHub → AI), a glance-first home screen, and two variants: **Tasky Desk** (a companion for your computer: volume, Claude Code agents) and **Tasky Go** (battery, phone notifications). See **[docs/ROADMAP.md](docs/ROADMAP.md)**. Code-level fixes and good first issues live in **[docs/IMPROVEMENTS.md](docs/IMPROVEMENTS.md)**.

## Contributing

PRs welcome: new screens, integrations, enclosure designs, translations, bug fixes. Read **[CONTRIBUTING.md](CONTRIBUTING.md)** first. CI builds both boards on every PR.

## License

[GPL-3.0](LICENSE). You can use, modify and sell Tasky, but derivative firmware must stay open under the same license.

Made by [@arampersand](https://github.com/arampersand).
