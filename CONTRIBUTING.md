# Contributing to Tasky

Thanks for helping make Tasky better. Bug reports, fixes, new screens, integrations, hardware variants and documentation are all welcome.

This guide covers how to set up, how to submit work, the coding conventions the firmware follows, and what "tested" means for an embedded project.

## Contents

- [Ways to contribute](#ways-to-contribute)
- [Development setup](#development-setup)
- [Branch and pull request flow](#branch-and-pull-request-flow)
- [Commit messages](#commit-messages)
- [Coding conventions](#coding-conventions)
- [Testing](#testing)
- [Proposing hardware changes](#proposing-hardware-changes)
- [Releases](#releases)
- [Code of conduct](#code-of-conduct)
- [License](#license)

## Ways to contribute

- **Report a bug.** Open an issue with the **Bug report** template. Include your board (C3 or C6), firmware version (**More > Update** shows it) and serial output if you can.
- **Suggest a feature.** Use the **Feature request** template. Describe the problem before the solution.
- **Fix something.** Look for issues labelled `good first issue` or `help wanted`.
- **Improve the docs.** Typos and unclear steps are worth a PR.

For anything bigger than a bug fix (a new screen, a new integration, a change to navigation or persistence), **open an issue first** so we can agree on the approach before you spend time on it.

## Development setup

Full instructions, including toolchain install, USB drivers and flashing, are in [docs/BUILDING.md](docs/BUILDING.md). The short version:

```bash
git clone https://github.com/<you>/tasky.git
cd tasky/firmware
pip install -U platformio
pio run -e xiao_c3            # compile for XIAO ESP32-C3
pio run -e xiao_c6            # compile for XIAO ESP32-C6
pio run -e xiao_c3 -t upload  # flash over USB
pio device monitor            # serial console at 115200
```

Optional: copy `firmware/tasky/config_secrets.h.example` to `config_secrets.h` to bake in your WiFi and API keys for local testing. That file is gitignored. Keep it that way.

Useful background:

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): how the firmware is structured
- [docs/CUSTOMIZING.md](docs/CUSTOMIZING.md): recipes for new screens, services, pins and themes
- [docs/HARDWARE.md](docs/HARDWARE.md): wiring and parts

## Branch and pull request flow

1. **Fork** the repository and create a branch from `main`:

   ```bash
   git switch -c fix/encoder-double-step
   ```

   Branch prefixes: `feat/`, `fix/`, `docs/`, `refactor/`, `chore/`, `hw/`.

2. **Keep the PR focused.** One fix or one feature per PR. Don't mix formatting-only changes into a functional change.
3. **Build both environments locally** before pushing (see [Testing](#testing)).
4. **Open a pull request** against `main` and fill in the template. Link the issue (`Closes #12`).
5. **CI must be green.** `.github/workflows/ci.yml` compiles `xiao_c3` and `xiao_c6`. A red build is not merged.
6. A maintainer reviews it. Expect questions, especially about flash and RAM use and hardware testing.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<optional scope>): <short summary in the imperative>

<optional body: what and why, not how>
```

| Type | Use for |
|---|---|
| `feat` | New user-visible behaviour |
| `fix` | Bug fix |
| `docs` | Documentation only |
| `refactor` | Code change without behaviour change |
| `perf` | Faster, smaller, or less RAM |
| `build` / `ci` | PlatformIO, scripts, workflows |
| `chore` | Version bumps and housekeeping |
| `hw` | Wiring, pin maps, enclosure files |

Common scopes: `ui`, `timer`, `tasks`, `calendar`, `weather`, `agent`, `portal`, `ota`, `net`, `model`, `web`.

Examples:

```
feat(ui): add air quality to the ambient screen
fix(portal): keep break length when saving the form
docs: explain ROW_OFFSET for 240x280 panels
```

## Coding conventions

Follow the style of the file you are editing. In addition:

### Naming

- `camelCase` for functions, methods, variables and struct fields: `handleTimer()`, `lastInputAt`, `cfg.workMin`.
- `PascalCase` for types: `TaskyModel`, `WeatherService`, `CalendarEventItem`.
- `UPPER_SNAKE_CASE` for macros, constants and enum values: `FEATURE_WEATHER`, `AMBIENT_IDLE_MS`, `SCR_TIMER`.
- Files: `snake_case` (`weather_service.cpp`), one class per `.h`/`.cpp` pair.

### Timing: `millis()`, not `delay()`

The UI must never freeze. Schedule periodic work with unsigned `millis()` deltas, which are safe across the 49-day wrap-around:

```cpp
static uint32_t lastAt = 0;
if (millis() - lastAt >= 1000) {
  lastAt = millis();
  // once per second
}
```

Don't add `delay()` to `loop()`, handlers, draw functions or services. The few existing ones (boot, panel reset, the Agent "thinking" pause, restart) are listed in [ARCHITECTURE.md](docs/ARCHITECTURE.md#main-loop-timing-model). Don't add to that list.

### Input: read once per loop

`loop()` reads the encoder and MODE button **once** and passes `step` and `click` to the current handler. Handlers must not call `digitalRead()` on input pins or call `encoderStep()` / `encoderClickType()` again, because that would steal or double-count events.

### Screens

Follow the `if (!ui.baseDrawn) { ui.drawX(); ui.baseDrawn = true; }` pattern. Navigate with `goTo()`, redraw only what changed, and keep content inside the 12 px safe margin. See [Adding a new screen](docs/CUSTOMIZING.md#adding-a-new-screen).

### Persistence

- NVS keys (namespace `tasky`) must be **15 characters or fewer** and unique. Check the table in [ARCHITECTURE.md](docs/ARCHITECTURE.md#persistence).
- Add a field in three places: the `Config` initializer in `model.h`, `load()` (same default), and `save()`.
- Never rename or reuse an existing key. Users would silently lose that setting on update.
- Call `model.save()` after user actions, not on a timer (flash wear).

### Optional code behind `FEATURE_` flags

Anything that is not core (Timer, Tasks, Stats, More, Settings, portal, OTA) must compile out cleanly. Guard the header body, the `.cpp` body, the global instance, the `begin()`/`loop()` calls, the screen, the More entry and the portal fields with `#if FEATURE_X`. A PR must build with your flag at both `1` and `0`.

### Memory

- Prefer fixed-size `char[]` buffers and `snprintf()` for anything stored or built repeatedly. Avoid growing `String`s in hot paths.
- Keep HTTP payloads small: ask APIs for only the fields you need, and stream large ones (see `ApiClient::getStreaming()`).
- Report the size change in your PR. `pio run` prints flash and RAM usage. The app must stay well under the 1.875 MB OTA slot.

### Secrets

- **Never commit** credentials, tokens, iCal URLs, WiFi passwords or personal data, including in screenshots and serial logs.
- `config_secrets.h` is gitignored. New secrets get a placeholder in `config_secrets.h.example` and an empty fallback in `config_features.h`.
- If you committed a secret by mistake, **rotate it first**, then tell a maintainer. Rewriting history is not enough.

### Comments and language

Write new code comments, identifiers, commit messages and docs in English. Some older comments and the setup portal are in Spanish. Translating them is a welcome, separate PR.

## Testing

There are no host-side unit tests yet. Correctness is checked by compiling and by testing on real hardware.

### Required for every PR

- [ ] `pio run -e xiao_c3` succeeds
- [ ] `pio run -e xiao_c6` succeeds
- [ ] No new compiler warnings in files you touched
- [ ] If you added or changed a `FEATURE_` flag: builds with it at `0` **and** at `1`
- [ ] Flash and RAM delta noted in the PR description

CI repeats the two builds. Your PR can't be merged until both are green.

### Hardware test checklist

Firmware changes that affect behaviour must be tested on a device. State in the PR which board (C3 or C6) and which display you used. Run the parts of this checklist that your change touches, and always run the first block.

**Boot and basics (always)**

- [ ] Boots to the splash screen and then the Timer with no reset loop (watch the serial monitor)
- [ ] Encoder turns step once per detent in the correct direction; click and long click (about 1 s) register
- [ ] MODE cycles Timer → Tasks → Agenda → Stats → More → Timer; double MODE goes back
- [ ] Display has no offset, noise band or wrong colours; dark and light themes both readable

**Timer and focus**

- [ ] Start, pause and reset; turning the encoder changes the duration in 5-minute steps while stopped
- [ ] At zero: "WELL DONE", buzzer and light bar blink; click goes to the next phase
- [ ] With focus lock on, MODE shows "Pause to unlock" while a work session runs

**Persistence**

- [ ] Change a setting and a task, power-cycle: values are kept
- [ ] Factory reset (both buttons for 3 s) restores defaults

**Network (if touched)**

- [ ] **More > Setup (QR)** brings up the `Tasky` network; the phone opens the portal automatically
- [ ] Saving the portal form with valid WiFi connects; time syncs (Ambient clock shows the correct time)
- [ ] Each integration you touched fetches data and fails gracefully without WiFi or with bad credentials
- [ ] The UI stays responsive while services fetch

**OTA (if touched)**

- [ ] An upload is rejected while Update is not armed
- [ ] Armed upload of `firmware.bin` succeeds, the device reboots, and the new version shows on **More > Update**

**Idle and notices**

- [ ] 5 minutes without input (no session running) shows Ambient; any input returns to the previous screen

## Proposing hardware changes

Hardware changes affect everyone who has already built a Tasky, so they need a bit more process:

1. **Open an issue first** with the `hardware` label. Describe the problem you are solving (cost, availability, a better display), the parts involved (exact part numbers and links), and whether it breaks existing builds.
2. **Keep the existing build working.** New pins or displays go behind `config_hardware.h` defines or a compile-time switch (as `DISPLAY_CIRCULAR` and `ACTIVE_BUZZER` do), with the current reference hardware as the default.
3. **Use XIAO `Dn` pin names**, not raw GPIO numbers, so one pin map works on both C3 and C6. Mention any strapping-pin or shared-pin consequences (D0 is shared by the buzzer and backlight; D9 is the LED and also SPI MISO).
4. **Update [docs/HARDWARE.md](docs/HARDWARE.md)** in the same PR: wiring table, BOM and photos or a schematic if the wiring changes.
5. **Show it working.** A photo or short video of the modified build running the PR's firmware.

## Releases

Maintainers cut releases. If your PR should ship in the next version, say so. A release is:

1. Bump `TASKY_VERSION` in `firmware/tasky/version.h` and add an entry to `CHANGELOG.md`.
2. `git tag vX.Y.Z && git push --tags`. `release.yml` refuses a tag that does not match `version.h`.
3. Actions publishes the GitHub Release and redeploys the web flasher.

Details: [Publishing your own fork](docs/CUSTOMIZING.md#publishing-your-own-fork-with-its-own-web-flasher).

## Code of conduct

Be kind. Assume good intent, critique code rather than people, and help newcomers. This project follows the [Contributor Covenant, version 2.1](https://www.contributor-covenant.org/version/2/1/code_of_conduct/). To report unacceptable behaviour, use GitHub's "Report content" option or contact the maintainer ([@arampersand](https://github.com/arampersand)) privately.

## License

By contributing, you agree that your contributions are licensed under the [GPL-3.0](LICENSE), the same license as the project.
