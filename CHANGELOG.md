# Changelog

All notable changes to the Tasky firmware. Versions match `firmware/tasky/version.h` and git tags.

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
