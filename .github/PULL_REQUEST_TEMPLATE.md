## What and why

<!-- What does this PR change, and why? Link the issue it resolves. -->

Closes #

## Type of change

- [ ] Bug fix
- [ ] New feature (screen, integration, setting)
- [ ] Refactor / performance (no behaviour change)
- [ ] Hardware support (pins, display, parts)
- [ ] Documentation
- [ ] Build / CI / web flasher

## Checklist

- [ ] Builds for **xiao_c3** (`pio run -e xiao_c3`)
- [ ] Builds for **xiao_c6** (`pio run -e xiao_c6`)
- [ ] Optional code is guarded by a `FEATURE_` flag and builds with that flag at `0` and `1`
- [ ] No `delay()` added to the loop, handlers, draw functions or services
- [ ] New NVS keys are 15 characters or fewer and don't reuse existing keys
- [ ] No secrets, tokens, WiFi credentials or personal URLs in code, logs or screenshots
- [ ] Docs updated (`docs/`, `README.md`, `CHANGELOG.md`) if behaviour, settings, pins or wiring changed
- [ ] Version bumped in `firmware/tasky/version.h` (release PRs only)

## Tested on hardware

- [ ] Yes
- [ ] No (explain why, e.g. docs-only or CI-only)

- **Board:** <!-- XIAO ESP32-C3 / XIAO ESP32-C6 -->
- **Display:** <!-- e.g. 1.54" ST7789 240x240 (reference) -->
- **What I tested:** <!-- items from the hardware test checklist in CONTRIBUTING.md -->

## Size impact

<!-- Paste the Flash / RAM lines from `pio run -e xiao_c3`, before and after. -->

| | Before | After |
|---|---|---|
| Flash | | |
| RAM | | |

## Screenshots or photos

<!-- For UI changes: a photo of the screen. -->
