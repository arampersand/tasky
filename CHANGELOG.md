# Changelog

All notable changes to the Tasky firmware. Versions match `firmware/tasky/version.h` and git tags.

## v3.1.0 — 2026-10-16 · Claude Code over Wi-Fi

### Added
- **Claude Code over Wi-Fi.** Plug Tasky into your computer once and the plugin pairs with it; from then on your session cards and knob approvals also work unplugged, on battery, anywhere on the same network. Tasky listens on TCP 7778 with a per-device token and answers a UDP broadcast so the plugin finds it when its address changes. Over Wi-Fi it only takes cards and questions, never settings.

### Changed
- About 215 KB smaller firmware on both boards: the QR library no longer pulls in C++ string streams.
- **Calmer Home:** the date in the top-left corner, the weather in the top-right, the clock in the middle, today's focus bar and **one** task (the one you're on, or the next open one). The next calendar event is on Agenda and Calendar.
- **GitHub streak shows all your contributions**, private ones included (when your profile shows them): Tasky reads your profile's contribution graph instead of the public events feed, which missed most days.
- **Controls remembers each device's mode** (Mac, Windows or Phone, up to 4 devices): switch from your Mac to your phone and the keys follow. A new device shows "Hold: Mac / Windows / Phone".
- **Phone mode: Home and Assistant keys** (home screen; Siri or Google Assistant). On computers, *Dictation* is now **Assistant** (Mac: the voice key; Windows: Copilot, Win+C).
- **tasky-flash.vercel.app/claude**: connect Claude Code step by step (English and Spanish), with a one-line Terminal command. The Agents screen and the setup point there.
- Calendar setup (web and phone) links straight to Google Calendar's settings page.

## v3.0.1 — 2026-10-16 · More in Settings

### Changed
- **More moved into Settings.** It's the first row of Settings instead of a separate last screen (they looked like the same thing). MODE from More goes back to Settings. If you take Settings out of the carousel, More comes back at the end so you can always reach it.
- New default order: Home → Timer → Tasks → Agenda → Calendar → Streak → Weather → Agents → Controls → Phrases → Settings → Setup (QR). A carousel you already arranged in Screens stays as it is.
- The page number in the corner is bigger.

## v3.0.0 — 2026-10-16 · One Tasky

### Changed
- **One firmware.** Tasky and Tasky for Agents are now the same firmware with everything: focus timer, tasks, calendar, weather, GitHub, Notion, AI, Claude Code agent cards and approvals, and the Bluetooth key row. The web flasher only asks for your board. Agents and Controls are in the carousel from the start; Bluetooth starts the first time you open Controls (then on every boot).
- `TASKY?` reports `"edition":"tasky"` with `"agents":true`; the companion and the web setup accept both this and 2.x Agents builds.

### Added
- **Bigger Setup (QR) code** that phone cameras pick up (large modules, white margin), with the network and password underneath for joining by hand.
- Calendar is in the default carousel.
- Faster, cleaner boot: the display is set to black first (the backlight is always powered on these boards, so it used to show noise for about a second).
- **More > Screens** on the device: choose the screen Tasky starts on, and tick, untick and reorder the carousel with the knob.
- **Daily brief** (with an AI key): the AI screen's default ("auto") mode is now **Today**: your day in three short lines from your calendar and tasks (what matters, what to start with, a tip). It's generated once a day and shown in the morning notice. The other nine modes stay in the mode picker. The screen is called **AI brief**.
- **Printable quick-start card** (English and Spanish) at tasky-flash.vercel.app/start: controls, setup in two minutes, everyday use, Claude Code.
- **Phone mode** in Controls (long press cycles Mac → Windows → Phone): volume and scroll dials, play/pause, next/previous, mute, take a photo (camera shutter), next/previous page.

## v2.13.0 — 2026-10-16 · Easy setup & polish

### Added
- **Claude Code plugin**: in Claude Code, `/plugin marketplace add arampersand/tasky` and `/plugin install tasky@tasky`. Six hooks (a **Ready** card when a session starts), and `/tasky:status`, which runs the new `tasky_agent.py doctor`: it checks Python, Tasky on USB (and which app holds the port), the firmware, and the hooks, then says what to fix. The plugin version follows the firmware's; `install.sh` stays as the terminal alternative. If both are installed, Tasky shows each question once.
- **Web setup helpers**: calendar steps for Google, iCloud and Outlook with link checks, and a **Test it** button (new `TASKY CALTEST` command: Tasky downloads your calendar and reports the events it found); GitHub "Is this you?" with your recent activity as green squares; the page goes to Set up by itself after flashing.
- **Where am I**: the screen's number in the carousel, small, in the bottom-right corner.
- **Home shows your day**: today's streak square and a focus bar toward a daily goal (120 min, `goal` in the setup protocol). A Tasky that isn't set up says **Set me up** with the web address.
- **Quick settings**: long press on Home (theme, light bar level, focus light). Tasks is in the carousel.
- **Streak: yours and GitHub's**, two pages you switch with the knob. Your streak counts Tasky activity; GitHub's counts your contributions over ~90 days.
- **Rest screen with quotes**: big clock, date and a quote with its author.
- **Agent banner** on any screen when an agent starts waiting (Tasky for Agents).
- Hints with knob icons, level bars in Settings (the light bar shows the level while you turn), a short light blip on every press, and a dimmer light bar at night.

### Changed
- Default carousel, by how often you use it: Home → (Agents → Controls →) Phrases → Timer → Tasks → Agenda → Streak → Weather → Settings → Setup (QR). Settings can be in the carousel.
- Weather without data says what's missing (city, Wi-Fi, updating) instead of showing the clock.
- `tasky_agent.py` lives in `claude-plugin/` (the plugin and `install.sh` share it).

### Removed
- The Streak screen's hidden "press to change a day's level".

## v2.12.0 — 2026-10-16 · Computer companion

> **Upgrading from 2.11 or older: update once from https://tasky-flash.vercel.app** (keeps your settings). This version has bigger app slots, which only the web flasher can set up. A Wi-Fi update is refused with a message that says so.

### Added
- **Approve Claude Code from the knob** (Tasky for Agents): a `PermissionRequest` hook shows what Claude wants to do on its card. Turn to pick *Allow*, *Deny* or *Allow edits*, press to answer, long press to leave it to the terminal. If nobody answers in 60 s, or Tasky isn't plugged in, the normal terminal prompt appears. Protocol: `TASKY ASK` / `ASK-CANCEL` and `@tasky {"answer":...}`.
- **Agent cards with Claude Code's pixel character**: one card per session. It walks while working (terracotta), waves while waiting for you (amber) and hops when done (green). Arrows and dots show there are more cards; turn the knob up for the next one.
- **About 50 quotes with their authors** on the Phrases screen (Stoics, founders, builders, motivation), in smaller type with the author underneath. Your own phrases can name an author: `Stay hungry - Steve Jobs`.
- **Streak gets greener as you use Tasky**: a finished focus session = 3 points, a task done = 2, an agent question answered = 1 (1 / 4 / 8 points for the three greens; GitHub contributions count too).
- Setup (QR) is a screen you can put in the carousel. Streak and Setup (QR) are in both editions' default carousel.
- **Controls is a row of computer keys** with **Mac / Windows** shortcuts (long press to switch): volume and brightness dials, mute, play/pause, next/previous, Mission Control / Task View, Spotlight / Search, dictation, desktop left/right, screenshot, lock.
- **One-command setup for agents**: `curl -fsSL https://tasky-flash.vercel.app/agents/install.sh | sh` adds the hooks to `~/.claude/settings.json` (with a backup; `--uninstall` removes them). The web setup has an **Agents** step with the command, a test card, and a Disconnect button that frees the USB port.
- Companion tests (`python3 -m unittest discover companion/tests`).

### Changed
- The companion runs a small background process that owns the USB port (hooks talk to it over `~/.tasky/agent.sock`), so a question can wait for the knob while other sessions keep reporting. No more `pyserial` dependency, and it never toggles DTR/RTS.
- Flash layout: two 1.9375 MiB app slots (`firmware/partitions.csv`, the unused SPIFFS removed). That's +64 KB for each firmware.
- Controls dials (volume, brightness): turning the knob up raises them.

### Fixed
- Days that passed while Tasky was off didn't move the streak on, so the next day's activity landed on yesterday's square. The day is now saved and caught up at boot.

## v2.11.0 — 2026-10-16 · Your screens & phrases

### Added
- **Pick your screens**: the web setup's new **Screens** step chooses which screens MODE cycles through and in what order (Home first, More last). Everything else is listed in More. Tasky reports the screens its edition has (`screenChoices` in `TASKY?`), and `TASKY SET {"screens":[...]}` saves them.
- **Phrases screen**: one phrase at a time, big and centered. Turn to browse, press for a random one, and it changes by itself every minute.
- **Your own phrases**: the web setup's **Phrases** step (up to 12) replaces the built-in phrases on Home and on the Phrases screen.
- Weather and Streak are in the carousel by default.

### Changed
- Streak uses GitHub's greens and repaints only the cells that change.
- Weather redraws by itself when new data arrives.

## v2.10.0 — 2026-10-16 · Smooth knob & screens

### Fixed
- **The knob moves one step per click.** It needed two clicks before, and fast turns lost steps while the screen was drawing. The encoder is now decoded on interrupts (`encoder.cpp`). Builders with a different encoder can set `ENCODER_TICKS_PER_DETENT` (2 or 4) in `config_hardware.h`.
- **No more full-screen flash when you move the selection** in Settings, Tasks, More, Focus and Calendar: only the rows that changed are repainted. Turning the knob on the Timer repaints just the digits.

## v2.9.0 — 2026-10-16 · Tour & hints

### Added
- **First-boot tour**: four interactive steps (turn the knob, press it, press MODE, done) that each wait for the action they teach. Hold MODE to skip; replay from **More → Tour**. Devices already in use skip it on upgrade.
- **On-screen hints**: the first three times you open a screen, a one-line hint at the bottom says what the knob does there. It hides after 3 s or on any input.
- Website: the USB port picker lists only Tasky (Espressif USB, vendor `0x303A`) instead of every serial port, says what to pick ("USB JTAG/serial debug unit"), shows when a known Tasky is plugged in, and explains that *Erase device* only wipes Tasky. "Show every port" turns the filter off.

## v2.8.0 — 2026-10-16 · Boards & RGB light

### Added
- **RGB light bar** for the v3.1 PCB (6 × SK6812MINI on D9), driven by the RMT peripheral with no new library. Choose it in the new **Light** step of the web setup (`TASKY SET {"light":1}`). The light blinks once to confirm. The default stays plain LEDs with PWM.
- `docs/HARDWARE.md`: the v1.1 and v3.1 carrier boards, the schematic and board notes. `docs/ROADMAP.md`: Tasky Go hardware plan based on the real boards.

### Fixed
- CI uploads the merged image again (it was looking outside `release/`).
- Web flasher: the Agents manifests are named "Tasky for Agents".

## v2.7.0 — 2026-10-15 · Tasky for Agents

### Added
- **Tasky for Agents**, a second firmware (envs `xiao_c3_agents`, `xiao_c6_agents`; pick it in the web flasher):
  - **Agents** screen: Claude Code sessions reported over USB by hooks (`companion/tasky_agent.py`); the light bar breathes and Tasky beeps when an agent waits for you; Home shows "1 agent needs you".
  - **Controls** screen: Bluetooth keyboard/media dial — volume, brightness, media, and shortcuts (Spotlight, Voice/Siri, screenshot, Mission Control, lock).
  - Drops the on-device AI chat and Notion to fit the Bluetooth stack.
- **Clock without internet:** the web setup sends your computer's time and time zone; the phone portal sends the phone's.
- Website: firmware picker (Tasky / Tasky for Agents), "Two firmwares" section, new studio renders.

## v2.6.0 — 2026-10-15 · Home & web setup (3.0 beta)

### Added
- **Home screen**: the new first screen. Clock, date and weather, next event, today's open tasks and a rotating phrase. Press the knob to start focusing; long press for Tasks.
- **Set up from the browser**: after flashing, https://tasky-flash.vercel.app walks you through Wi-Fi (with a network scan), calendar, tasks, GitHub, AI and weather over the USB cable. The time zone comes from your computer.
- USB setup protocol (`TASKY?`, `TASKY SCAN`, `TASKY SET {json}`) in `usb_setup.cpp`.
- The 3D model on the website shows the Home screen.

### Changed
- Ambient (idle) uses the Home layout and never triggers while you're already on Home.
- New library dependency: ArduinoJson 7.

## v2.5.0 — 2026-10-15 · Portal login

### Security
- **Per-device setup-network password**: 10 random characters generated on first boot, shown under the QR code and on the WiFi/Update screens. The fixed `GetItDone` password is gone; a factory reset generates a new one.
- **Portal login from the home network**: HTTP Basic auth (`tasky` + the device password). Clients on the setup network don't need it.
- Requests from the home network must target the device IP and come from the same origin (blocks CSRF from other web pages and DNS rebinding).
- URLs in serial logs are trimmed to the host, so the private iCal address no longer leaks into logs.
- Factory reset also erases the WiFi driver's stored credentials.

### Added
- `docs/ROADMAP.md`: Tasky 3.0 plan, Desk and Go variants, early user feedback.

> **Upgrading:** after updating, your Tasky's setup-network password changes. Read it under the QR code (More → Setup (QR)).

## v2.4.1 — 2026-10-15 · Portal hardening

### Security
- The setup portal never sends saved secrets back to the browser (WiFi password, calendar URL, API keys). Fields are write-only: empty keeps the value, "borrar" clears it.
- Every value shown in the portal is HTML-escaped (fixes corruption of values containing `'` and HTML injection).
- The setup network and portal turn off after 10 minutes without activity instead of staying up until reboot.
- Connecting to your home WiFi no longer re-enables the setup access point (`WIFI_STA` instead of `WIFI_AP_STA`).
- The AP password is no longer printed in the portal footer.

### Fixed
- Saving the portal no longer un-completes tasks whose names didn't change.

## v2.4.0 — 2026-10-15 · First open-source release

### Added
- Focus mode rework: light bar on and MODE locked while a work session runs (configurable).
- Workday hours with a morning brief and an end-of-day close.
- Focus minutes tracked per task; the timer remembers the task you're working on.
- Calendar helpers: next event today, first event on a given day.
- Single merged flash image per board (`tasky-<board>-<version>.bin`) and a browser flasher at https://tasky-flash.vercel.app.
- CI builds XIAO ESP32-C3 and ESP32-C6 on every PR; tagged releases publish binaries + `SHA256SUMS`.
- `config_secrets.h.example`, full documentation set, GPL-3.0 license.

### Removed
- DNS-based site blocking during focus (`focus_dns`) and the experimental `power_manager`.

### Changed
- Repository restructured into `firmware/`, `web/` and `docs/`.

### Fixed
- OTA upload from the Wi-Fi portal always failed with "Not enough space" (size was unknown at upload start).
- Portal footer showed v2.2 and a wrong repository link.

## v2.3.0 — 2026-02-25
- Major UX overhaul: English UI, new screens, service timing fixes.
- UI polish: calendar fix, theme, "Add task", agenda auto-refresh, encoder direction and colour tuning.
- User mode without visible API keys; differentiated Focus Mode; less flicker.
- Music player, weather and agenda.

## Earlier (Dec 2025)
- Circular/square display base firmware, timer and navigation, calendar parsing, first stable version.
