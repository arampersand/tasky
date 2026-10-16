# Roadmap

Where Tasky is going next. [IMPROVEMENTS.md](IMPROVEMENTS.md) tracks code-level fixes; this file is about the product. Goal of the next phases: **a stable, delightful 3.0** that someone can unbox, flash and use in five minutes.

## What early users told us

From the first people who built and flashed a Tasky (Oct 2026):

| Feedback | What it means for Tasky |
|---|---|
| "The web flasher is 10/10." | Keep the browser as the front door: flash **and** set up there. |
| "Taking it out of the bag and plugging it into a port is a deal-breaker. A battery is a must." | A portable, battery-powered variant. |
| "I want to glance at the weather, the time and my to-dos without opening five apps. The Pomodoro is a bonus." | A glance-first home screen; the timer is one turn away. |
| "It has to be a gadget you *want* to use, like the Rabbit R1, not something that eats ports, space and setup time." | Zero-config first run, fewer menus, nicer setup. |
| "You need a nice page to configure it, and a manual. Picking it up and pressing things is confusing." | A guided web onboarding, on-device hints, a printable quick-start card. |
| "Connect your calendar and see the things you have to do." | Calendar items show up as today's to-dos. |
| "Make it work for the computer: volume, brightness, Siri/search, switching Claude agents, opening issues." | A desk-companion firmware variant. |
| "The little phrases on the idle screen were great; make that the main screen." | Home = clock + weather + to-dos + a rotating phrase. |
| "Show my phone's notifications over Bluetooth." | Phone notifications on Tasky Go. |

## Two Taskys, one codebase

The variant is a build flag (`TASKY_VARIANT_DESK` / `TASKY_VARIANT_GO` in `config_hardware.h`) plus hardware; each gets its own PlatformIO env and its own button in the web flasher.

| | **Tasky Desk** | **Tasky Go** |
|---|---|---|
| Power | USB-C | LiPo battery (the XIAO C3/C6 have a built-in charger on the `BAT` pads) |
| Home screen | Home (glance) → Focus | Home (glance), screen sleeps until you touch the knob |
| Network | Always connected | Wakes, syncs every 15 min, Wi-Fi off in between |
| Talks to | Your computer (companion mode) | Your phone (notifications) |
| Display | 1.54" IPS (or round GC9A01A) | IPS with aggressive dimming, or **e-paper** for days of always-on glance |
| Battery target | — | A work week per charge (e-paper) / 1–2 days (IPS) |

**Hardware notes for Tasky Go**

The PCBs already have most of it (see [HARDWARE.md → Boards](HARDWARE.md#boards-pcb)): a 2000 mAh LiPo on the XIAO `BAT` pads (charged over USB-C) and, on v3.1, a slide switch. What Go still needs:

1. **Battery gauge.** One ADC1 pin reading `BAT+` through a 1:2 divider (2 × 220 kΩ + 100 nF). ADC2 (D3 on the C3) can't be used while Wi-Fi is on, and D0–D2 are taken. Proposed v3.2 change: tie the display's RES to 3V3 through an RC (the ST7789 has a software reset, `TFT_RST -1`), move the buzzer driver from D0 to D3 (digital is fine there) and use **D0 (A0) for battery sense** on both C3 and C6.
2. **Display backlight on a pin** (a MOSFET on BLK, which is tied to VCC today), so the screen can dim and sleep.
3. **Firmware:** battery icon and level on Home, light sleep between knob events, Wi-Fi only in short sync bursts, screen off after a timeout.
- Rough budget (IPS): Wi-Fi active ≈ 80–100 mA, backlight ≈ 20–40 mA. The 2000 mAh cell lasts ~15 h fully on (estimate), so a full day needs light sleep, short Wi-Fi bursts and a sleeping screen.

## Phase 1 — Stable 3.0: setup that flows *(in progress)*

**Shipped in 2.6–2.7:** clock from the browser/phone (no internet needed), web setup wizard over USB (Wi-Fi scan, calendar, tasks, GitHub, AI, weather, time zone), Home glance screen with rotating phrases as the first screen, Ambient sharing the Home layout. **Still to do:** Improv Wi-Fi inside the installer dialog, on-device first-boot tour + hints, portal in English/Spanish, quick-start card, hardware validation.

**Guided onboarding on the website**, one screen at a time, each step skippable:

1. **Flash** (as today).
2. **Wi-Fi** right in the browser: Tasky scans nearby networks over USB, you pick one and type the password. No captive portal needed. (Later: [Improv Wi-Fi](https://www.improv-wifi.com/) inside the installer dialog too.)
3. **Calendar**: paste your Google/Outlook/iCloud iCal link (with a 3-step "where do I find it" helper). Today's events appear as to-dos.
4. **GitHub** username (optional): your streak.
5. **AI** (optional): ChatGPT or Claude key, so you can ask Tasky things.
6. **City** for weather (auto-detected from the browser, editable).
7. **Done**: "turn the knob to look around, press to choose".

All of it goes over Web Serial, so it works from the same page that flashed the device. The captive portal (QR) stays as the phone-only fallback, with the same steps and in English/Spanish.

**On the device**
- **Home screen = glance:** big clock, weather, next event, today's to-dos and a **rotating phrase** ("Every second counts", …). Focus timer is the second screen.
- One-line hints on every screen ("turn = choose · press = start").
- First-boot tour of the knob and the MODE button (three screens).

**Docs**: printable quick-start card (PDF), 60-second video, manual reorganized around "what do I want to do".

**Exit criteria for 3.0**: flashed + set up in < 5 minutes by someone who has never seen a Tasky; a week of daily use without a reboot; every flow tested on real hardware (C3 and C6).

## Phase 2 — Your day on the screen
- **Calendar to-dos:** today's events listed as checkable items; calendars that export tasks (`VTODO`) add them too. Google Tasks / Todoist / Notion sync into the task slots.
- Agent screen with quick prompts ("plan my day", "summarize my next meeting") and answers that fit the screen.

## Phase 3 — Desk companion (computer) *(started)*

**Shipped in 2.7.0:** the **Tasky for Agents** firmware: Claude Code status over USB via hooks (`companion/`), breathing light when an agent waits, and a Bluetooth Controls screen (volume, brightness, media, shortcuts). Next: a long-running companion that lets the knob switch/focus terminal sessions, Windows/Linux shortcut sets, more agents (Codex, Cursor, CI).

The XIAO C3/C6 have no native USB (USB-Serial/JTAG only), so they can't be a USB keyboard. Two routes that work on both:
- **Bluetooth keyboard / media controller (BLE HID)**, no app needed: knob = volume (or brightness, switchable), press = play/pause, long press = mute. Extra actions mapped to shortcuts: Siri / Spotlight search, dictation, screenshot.
- **Companion app** (tray app or `tasky` CLI) over USB serial or Wi-Fi, for things a keyboard can't do:
  - **Claude Code / AI agents:** a [Claude Code hook](https://docs.anthropic.com/en/docs/claude-code/hooks) tells Tasky when an agent is working, done or **waiting for your approval**. The light bar pulses when an agent needs you; the knob switches sessions; a press focuses that terminal.
  - **Quick actions:** open a GitHub issue from a template, start a meeting transcript, toggle Do Not Disturb when a focus session starts.
- Shipped as an alternative firmware ("Tasky Desk: Companion") selectable in the web flasher.

## Phase 4 — Tasky Go
- ~~Battery wiring, switch~~ (on the v1.1/v3.1 PCBs). Still to do: battery gauge, backlight control, light/deep sleep (v3.2 board, see above).
- ~~SK6812 RGB light bar of the v3.1 board~~ (2.8: web setup **Light** step).
- **Phone notifications over Bluetooth:** iPhone via ANCS (no app needed); Android via a small companion app (or Gadgetbridge-style bridge). Filtered to what matters: calls, calendar, chosen apps.
- Enclosure files (STL/STEP) for Desk and Go in `hardware/`, plus the EasyEDA carrier boards (Gerbers + BOM) so anyone can order one.
- E-paper experiment for the Go display.

## Ideas parking lot
- Pick a task with the knob; the timer logs focus minutes to it in Notion.
- "Stand up / drink water" nudges during long focus streaks.
- Meeting countdown on the light bar (fills up as the meeting approaches).
- Shared streaks between friends' Taskys.
- Voice notes to your task list (needs a mic; Go v2).

Want to work on one of these? Open an issue to claim it, see [CONTRIBUTING.md](../CONTRIBUTING.md).
