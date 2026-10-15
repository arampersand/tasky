# Tasky — improvement proposals

An audit of the firmware (`firmware/tasky/`, v2.4.0), the build setup (`firmware/platformio.ini`), CI (`.github/workflows/`) and the web flasher (`web/`). Each proposal lists **Why** (with file:line evidence), **What**, **Effort** (S < 1 day, M = a few days, L = a week or more) and **Risk** (including whether you need to test it on real hardware).

Baseline at the time of writing: the app image is 1,611,200 B (C3) and 1,619,888 B (C6). The `min_spiffs.csv` OTA slot is 0x1E0000 (1,966,080 B), so the slot is about 82 % full.

---

## Quick wins (do first)

| # | Item | Effort | Why first |
|---|------|--------|-----------|
| 1 | ~~**Fix portal OTA size** ([K1](#known-issues))~~ ✅ fixed in v2.4.0 | S | OTA over Wi‑Fi works again (needs a hardware check) |
| 2 | **Stop leaking secrets from the portal** ([S1](#s1-portal-never-echo-secrets-escape-all-output)): write-only secret fields, HTML-escape values, stop the portal when you leave the screen | S | Anyone on the AP or LAN can read the Wi‑Fi password and API keys |
| 3 | **Persist the day rollover, and stop the portal clearing done-state** ([K5](#known-issues), [K9](#known-issues), [K10](#known-issues)) | S | Silent data bugs that users hit every day |
| 4 | **Bound HTTP payloads**: Notion `page_size`, GitHub `per_page=30`, a TLS handshake timeout ([R2](#r2-bounded-http-timeouts-and-payloads)) | S | Removes the most likely OOM and multi-second UI freezes |
| 5 | **Pin `lib_deps` versions** ([C1](#c1-reproducible-builds-pin-lib_deps)) | S | Releases are not reproducible today |

---

## Known issues

These are real bugs, confirmed in the code. Where a finding depends on hardware wiring, it says so.

| ID | Sev | Where | Problem | Fix |
|----|-----|-------|---------|-----|
| K1 ✅ | Fixed in v2.4.0 | `wifi_portal.cpp:600`, `ota_service.cpp:40` | `WebServer` sets `upload.totalSize = 0` at `UPLOAD_FILE_START` (core `Parsing.cpp:497`), so `startUpdate()` always gets the 3,145,728 fallback. `Update.begin()` rejects any size larger than the 0x1E0000 slot, so every portal OTA fails with "Not enough space". | `ota->startUpdate(UPDATE_SIZE_UNKNOWN)`. Optionally check `Content-Length` against `esp_ota_get_next_update_partition()->size`. |
| K2 | High | `wifi_portal.cpp:248,298,337,393,446`, `:560` | The portal sends the Wi‑Fi password, Notion key, Firebase secret, Spotify secret and LLM key back in `value='…'`, and prints the AP password in the footer. It is served over plain HTTP on the AP **and** on the STA IP: `tasky.ino:628` even advertises `http://<lan-ip>/`. | See [S1](#s1-portal-never-echo-secrets-escape-all-output). |
| K3 | High | `tasky.ino:264,400,533,547,561`, `wifi_portal.cpp:69` | `portal.begin()` is called from five places and `portal.stop()` from none. Once you open QR, Settings → WiFi or Update, the AP (`Tasky`/`GetItDone`, public in this repo), DNS hijack and config server stay up until reboot. | Stop the portal when you leave those screens, or after N minutes idle. |
| K4 | Med | `wifi_portal.cpp:257` (and every `value='"+x+"'`) | Values are not escaped. A task such as `It's done` or a Wi‑Fi password containing `'` breaks the attribute. The next **Save** posts back a truncated value, which corrupts the task or **breaks the Wi‑Fi connection**. The same gap allows HTML/JS injection. | HTML-escape every reflected value ([S1](#s1-portal-never-echo-secrets-escape-all-output)). |
| K5 | Med | `model.h:200`, `model.cpp:373` | `lastKnownDay` lives only in RAM and is seeded from the current day at boot. If the device is off at midnight, the streak never advances and yesterday's `dailyPomodoroCount`/`dailyFocusMin` carry over. A gap of several days advances only one cell. | Persist an epoch-day (`time/86400` after the TZ offset) and advance by the delta, capped at `STREAK_N`. |
| K6 | Med | `ui.cpp:46-48`, Adafruit `Adafruit_ST7789.cpp:157-162`, `Adafruit_ST77xx.cpp:218` | `ROW_OFFSET`/`COL_OFFSET` have no effect. `setOffsets()` → `setColRowStart()` sets only `_colstart/_rowstart`. Rotation 2 uses `_colstart2/_rowstart2`, and the call comes **after** `setRotation(2)`, so it is never applied. | Subclass and set `_colstart2/_rowstart2` too, then call `setRotation()` again. Or add the offsets in a custom `init`. Needs a panel with a real offset to test. |
| K7 | Med (HW) | `config_hardware.h:36-37`, `tasky.ino:103-107,119-122,1166-1171` | `TFT_BL` and the active `BUZZER_PIN` are both D0. `setBacklight()` drives D0 HIGH every 2 s and after every beep. That would sound an active buzzer wired directly to the pin. Brightness is only on/off, so the 20–255 Settings slider (`tasky.ino:500`) does nothing. | Confirm the wiring. Move the backlight to its own pin, use LEDC PWM for brightness, or remove the setting. |
| K8 | Med (HW) | `config_hardware.h:29-33`, `tasky.ino:1097` | `LED_PIN` is D9 = GPIO9 on the XIAO C3, which is the BOOT strapping pin. An LED circuit that pulls it low at reset puts the chip into download mode, so the device "won't boot". | Use a non-strapping pin, or make sure the LED circuit idles high or floating. Document this in HARDWARE.md. |
| K9 | Med | `wifi_portal.cpp:635-636` | Every portal Save sets `taskDone[i] = false` for all three tasks, even when the name is unchanged. Editing only the Wi‑Fi settings un-completes the day's tasks. | Reset `taskDone`/`taskFocusMin` only when the name changes. |
| K10 | Low | `model.cpp:115` vs `tasky.ino:335,494` | The device UI allows breaks of 1–30 min, but `load()` clamps `breakMin` to 5–90, so a 1–4 min break becomes 5 after a reboot. | Use one range in one place, for example `constexpr` limits in `model.h`. |
| K11 | Low | `tasky.ino:331` vs `:491,494` | Turning the encoder on the Timer screen changes the duration in the opposite direction from Settings (`-step` vs `+step`). | Pick one convention. |
| K12 | Low | `tasky.ino:565-566`, `ui.cpp:1996-2000` | `drawWifi(…, status, …)` passes the status string as the `ssid` argument, so the screen shows `AP: Status: Online` instead of the AP SSID. | Pass `portal.apSSID`. Show the status separately. |
| K13 | Low | `tasky.ino:117,133,1098` | The passive-buzzer path (`ACTIVE_BUZZER 0`) calls `ledcWriteTone()` on a pin that was never attached, because `ledcAttach(BUZZER_PIN…)` is commented out. In core 3.x it produces no sound. | `#if !ACTIVE_BUZZER ledcAttach(BUZZER_PIN, 2000, 8); #endif` |
| K14 | Low | `ui.h:10-14`, `platformio.ini:26-29` | `DISPLAY_CIRCULAR 1` includes `Adafruit_GC9A01A.h`, but the library is not in `lib_deps`, so that build variant fails. | Add `adafruit/Adafruit GC9A01A` behind an env, or a `-D DISPLAY_CIRCULAR=1` env ([A6](#a6-one-place-for-hardware-config)). |
| K15 | Med | `llm_service.cpp:622-643` | The hand-rolled Anthropic client reads the body with `while (client.available())`. It stops at the first empty TLS read, which truncates the response, and it does not decode `Transfer-Encoding: chunked`. | Use `HTTPClient`, as the OpenAI path does (`:646`). |
| K16 | Med | `llm_service.cpp:473-477,490-494` | The request JSON escapes only `"` and `\n`. A backslash, tab or control character in a task or event title produces invalid JSON, and the user silently gets the offline fallback. | Do proper JSON string escaping, or build the request with ArduinoJson ([A3](#a3-replace-hand-rolled-json-with-arduinojson)). |
| K17 | Low | `tasky.ino:948-959,998`, `llm_service.cpp:579` | `sendDailyPrompt()` is synchronous. The "Thinking… Press to cancel" state can never be seen or cancelled, the UI freezes for up to about 15 s, and an artificial `delay(800)` adds to that. | [R1](#r1-take-network-io-off-the-ui-loop). Remove the delay. |
| K18 | Med | `calendar_service.cpp:494-497,131-259` | RRULE `COUNT` is parsed but never enforced, so finite series repeat forever. `BYDAY` (for example `WEEKLY;BYDAY=MO,WE,FR`), `EXDATE` and `RECURRENCE-ID` are ignored, so cancelled or moved instances still show. | [T1](#t1-host-side-unit-tests-native-env) fixtures, then implement. |
| K19 | Med | `api_client.cpp:85,150-166`, `calendar_service.cpp:305-311` | `getStreaming()` stops at 100 KB but returns `true`. Google feeds are ordered by creation date, so a large calendar silently loses recent events, and the fetch still counts as a success. | Return a truncation flag and show "calendar too large". Also consider a calendar-specific export URL. |
| K20 | Med | `github_service.cpp:28-31`, `notion_service.cpp:46-49` | GitHub `events?per_page=100` and a Notion query with body `{}` (default `page_size` 100) are buffered whole in a `String` (`api_client.cpp:39`). Both can run to hundreds of KB, which is more than the free heap on a C3 with TLS active. GitHub also counts every nested `"created_at"` (PR, issue and comment objects) and compares UTC dates with local dates. | [R2](#r2-bounded-http-timeouts-and-payloads), [A3](#a3-replace-hand-rolled-json-with-arduinojson). |
| K21 | Low | `wifi_portal.cpp:278-291` | There is no GitHub token field. It can only be set through `config_secrets.h` (`model.cpp:148`). Notion fields appear only when `TASKY_MODE_DEV` is set (`wifi_portal.cpp:294`, `config_features.h:6`), so a release build ships the Notion code but users cannot configure it. | Add the fields, or drop `FEATURE_NOTION` from user builds. |
| K22 | Low | `network_manager.cpp:39`, `model.h:54`, `calendar_service.cpp:43-60` | The time zone is a whole-hour `int8_t` with no DST. India (+5:30), Nepal and Newfoundland can't be represented, and clocks plus iCal UTC conversions are off by an hour for half the year in DST regions. | [R3](#r3-time-posix-tz-and-persisted-day). |
| K23 | Low | `wifi_portal.cpp` (all copy), `llm_service.cpp:55-111,458` | The portal and AI agent replies are in Spanish, and the agent is forced to answer in Spanish. The device UI and docs are in English. Accented characters in replies render as garbage because Adafruit GFX fonts are 7-bit. | [U1](#u1-i18n-and-non-ascii-text). |
| K24 | Low | `tasky.ino:1237-1240` | Factory reset clears only the `tasky` NVS namespace. The `llm` namespace keeps its cache, and the Wi‑Fi driver's own copy of the SSID and password stays in `nvs.net80211`, because `WiFi.persistent` defaults to true (core `WiFiGeneric.cpp:370`). That copy survives a "reset before giving the device away". | Also call `WiFi.eraseAP()` and clear `llm`, or call `nvs_flash_erase()` and restart. |
| K25 | Low (HW) | `network_manager.cpp:13` | Every STA connect sets `WIFI_AP_STA`, which also starts the soft-AP with the last persisted AP config. On a device whose portal was never opened, that may be the IDF default open `ESP_xxxxxx`. It also disables modem sleep. **Verify with a Wi‑Fi scanner.** | Use `WIFI_STA` and switch to `WIFI_AP_STA` only inside `portal.begin()`. |
| K26 | Low | `api_client.cpp:21,57,179`, `calendar_service.cpp:318` | Full URLs go to the serial log, including the **secret iCal address**. Users paste these logs into GitHub issues. | [S6](#s6-log-hygiene). |
| K27 | Low | `model.cpp:303-306` vs `:459-483`, `github_service.cpp:114` | Completing a task bumps `streak[today]`, but the next pomodoro or GitHub sync overwrites the value through `updateStreakIntensity()`. | Count completed tasks in the intensity formula. |

These were already fixed during the release work, so they are not listed above: the portal footer version and link (`wifi_portal.cpp:559`, now uses `TASKY_VERSION`), and release assets (an app-only `-ota.bin` now ships, and the web flasher writes the individual parts so NVS is preserved).

---

## What is already fine

- **OTA requires physical confirmation.** Uploads are refused unless you arm them on the device, and arming lasts 5 minutes (`ota_service.h:41-47`, `ota_service.cpp:20`). For a hobby device this is a sound design. Keep it.
- **The iCal parser streams** line by line with byte, time and idle limits (`api_client.cpp:84-147`), so memory use is bounded.
- **NTP and Wi‑Fi connect are non-blocking state machines** (`network_manager.cpp:30-89`).
- **The timer hot path** uses `char` buffers and partial redraws (`ui.cpp:269-294`). `String` churn there is not a problem.
- **The task watchdog is not a crash risk today.** The idle-task check is disabled in the prebuilt sdkconfig (`# CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0 is not set`) and `loopTask` is not subscribed. Long blocking calls freeze the UI but do not reset the chip.
- **Build hygiene**: secrets come through `__has_include` (`config_features.h:24`), there is a single `version.h` that `release.yml` checks against the tag, the pioarduino platform is pinned, and dual OTA slots are sized correctly.
- **NVS wear is not a concern.** `model.save()` rewrites about 140 keys (`model.cpp:162-245`), but IDF NVS skips writes whose value is unchanged. The cost is speed, not flash wear (see [A4](#a4-split-config-and-persistence)).

---

## Security

### S1. Portal: never echo secrets, escape all output
- **Why:** K2, K3, K4. Secrets travel in cleartext to anyone on the AP, whose password is public in this repo, or on the home LAN. A Wi‑Fi password containing `'` is corrupted on re-save.
- **What:** Make secret inputs write-only. Render them as `placeholder='•••• set'` with an empty value, and keep the stored value when the field comes back empty. HTML-escape every reflected value. Stop the portal when you leave QR, WiFi or Update, or after 10 minutes without requests.
  ```cpp
  static String esc(const String& s) {           // attribute + text safe
    String o; o.reserve(s.length() + 8);
    for (char c : s) switch (c) {
      case '&': o += "&amp;"; break;  case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;   case '\'': o += "&#39;"; break;
      case '"': o += "&quot;"; break; default: o += c; }
    return o;
  }
  // handleSave: if (server.hasArg("lKey") && server.arg("lKey").length()) cfg.llmApiKey = server.arg("lKey");
  ```
- **Effort:** S. **Risk:** Low. Do a quick phone test of the captive portal.

### S2. Per-device AP password, session PIN, CSRF
- **Why:** The default AP credentials `Tasky`/`GetItDone` are hard-coded (`wifi_portal.h:20-21`) and published. `/save` and `/ota/upload` (`wifi_portal.cpp:23-44`) need no auth and no Origin check. Any web page you visit on the same LAN can blind-POST a form to `http://<tasky-ip>/save` and change the Wi‑Fi settings, because a simple form POST does not need CORS. The portal also answers on the STA interface.
- **What:**
  1. On first boot, generate a 10-character password from `esp_random()` and store it in NVS. Show it, and the Wi‑Fi QR, on the existing QR screen (`ui.cpp:2014`). Escape `\ ; , : "` in the `WIFI:` payload (`ui.cpp:2018`), because a generated password can contain them.
  2. Each time the portal starts, issue a 6-digit session PIN shown on the device. Require it once, then set a cookie. Reject requests whose `Origin`/`Referer` host is not the device IP or `192.168.4.1`, which also blocks DNS rebinding.
  3. By default, serve config only on the AP interface, and offer the LAN only while Update is armed.
- **Effort:** M. **Risk:** Med. Test captive-portal flows on iOS and Android.

### S3. TLS validation instead of `setInsecure()`
- **Why:** Every client calls `setInsecure()`: `api_client.cpp:8`, `llm_service.cpp:118,601`, `weather_service.cpp:6`, `firebase_service.cpp:7,105,128`, `spotify_service.cpp:12`. On a hostile network, an attacker can capture the LLM key (sent as `x-api-key`, `llm_service.cpp:613`), the Notion and GitHub tokens, and the Firebase secret, which sits in the query string (`firebase_service.cpp:95-96`).
- **What:** Use the Mozilla CA bundle. The prebuilt core already has `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y`. Embed the bundle and attach it once in `ApiClient`:
  ```ini
  board_build.embed_files = data/x509_crt_bundle.bin   ; from esp-idf gen_crt_bundle.py
  ```
  ```cpp
  extern const uint8_t bundle_start[] asm("_binary_data_x509_crt_bundle_bin_start");
  extern const uint8_t bundle_end[]   asm("_binary_data_x509_crt_bundle_bin_end");
  client.setCACertBundle(bundle_start, bundle_end - bundle_start);
  client.setHandshakeTimeout(10);  // default is 120 s (NetworkClientSecure.cpp:41)
  ```
  Use the full bundle (about 65 KB of flash), because iCal URLs can point at any host. **Do not pin certificates.** Leaf and intermediate rotation (Google and Anthropic rotate) would break devices in the field until they get an OTA. Certificate validity checks need a correct clock, so gate requests on NTP sync; GitHub (`github_service.cpp:31`) and LLM do not check it today.
- **Effort:** M. **Risk:** Med. Check flash headroom ([P4](#p4-flash-budget-and-partitions)). Test every integration on the device.

### S4. Secrets at rest (NVS plaintext)
- **Why:** The keys `wps`, `lKey`, `nkey`, `ght`, `fbKey` and `spSec` are stored in clear text (`model.cpp:172-219`, `# CONFIG_NVS_ENCRYPTION is not set`). With USB access, `esptool read_flash` dumps them.
- **What and tradeoffs:**
  - **NVS encryption with an HMAC-derived key**: the C3 and C6 have the HMAC peripheral. You need `custom_sdkconfig` in pioarduino (a full IDF lib rebuild) and an eFuse key burned once, which is irreversible per device. It protects against flash dumps but not against firmware you load yourself.
  - **Flash encryption**: irreversible eFuses. In Release mode it blocks plain reflashing, which breaks the web flasher. Development mode limits the reflash count. It is a poor fit for a maker project.
  - **Recommendation:** Don't enable either by default. Document the threat model: a desk gadget with physical access counts as compromised. Push for scoped credentials: a Notion integration limited to one DB, an LLM key with a spend cap, and no GitHub token at all, since public events don't need one. Add a "wipe secrets" item on the device (K24). Offer an opt-in `env:xiao_c3_secure` for makers who want NVS encryption.
- **Effort:** S (docs and wipe), L (encrypted env). **Risk:** High for flash encryption (risk of bricking, needs hardware). Low for the docs.

### S5. OTA hardening: validation, rollback, signed pull-updates
- **Why:** The image is checked only for magic and checksum by `Update`. Rollback is compiled in (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`), but Arduino marks the new app valid immediately at boot. As a result, a firmware that boots but cannot reach the UI or Wi‑Fi still sticks.
- **What:**
  1. Health-check rollback: override the weak hook and confirm only once the device is healthy.
     ```cpp
     extern "C" bool verifyRollbackLater() { return true; }
     // in loop(), once: UI drawn + 30 s uptime (+ WiFi connected if configured)
     esp_ota_mark_app_valid_cancel_rollback();
     ```
  2. Validate before `Update.end()`: check that the `esp_app_desc_t` at offset 0x20 has magic `0xABCD5432` and the right chip ID. Warn on a downgrade.
  3. Pull-OTA from GitHub Releases, keeping physical arming: fetch `latest.json` from the flasher site, stream `-ota.bin` straight into `Update` (no RAM buffer), and verify SHA-256 against `SHA256SUMS`. Then verify an **ed25519 signature** over that hash with a public key compiled into the firmware, signed in `release.yml` with a key from repo secrets. This gives most of the benefit of Secure Boot v2 without irreversible eFuses. It depends on S3.
- **Effort:** S (1), S (2), L (3). **Risk:** Med/High. Brick risk is mitigated by (1). Test on both chips, including a deliberately broken image.

### S6. Log hygiene
- **Why:** K26.
- **What:** Log host and path only (`url.substring(0, url.indexOf('?'))`), and drop the iCal URL log entirely. Use `log_i()`/`log_d()` with `CORE_DEBUG_LEVEL` so release builds stay quiet.
- **Effort:** S. **Risk:** Low.

---

## Reliability

### R1. Take network I/O off the UI loop
- **Why:** Services run synchronously from `loop()` (`tasky.ino:1174-1201`). A calendar fetch can take up to 20 s (`api_client.cpp:86`), LLM up to 15 s (`llm_service.cpp:622`), and a TLS handshake up to 120 s by default. The Agenda click calls both Calendar and Notion inline (`tasky.ino:806-813`). While that happens, the encoder and buttons are not polled: inputs are lost, the timer display stalls, and the alarm stops beeping.
- **What:** Create one FreeRTOS `net` task (8 KB stack, priority 1) that consumes a queue of jobs (`REFRESH_CAL`, `ASK_LLM`, …). Services parse into staging structs and publish them under a mutex or with a swap. The UI only reads snapshots and shows a "syncing" state. On a single-core C3, the scheduler still preempts on blocking socket I/O, so the UI stays responsive. After that, call `enableLoopWDT()` as a safety net.
- **Interim (S):** Set `setHandshakeTimeout(10)` and `http.setConnectTimeout(5000)`, and let at most one service run per 5 s tick.
- **Effort:** M–L. **Risk:** Med (shared-state races). Run a long soak test on hardware.

### R2. Bounded HTTP timeouts and payloads
- **Why:** K20. `ApiClient::get()` has no explicit timeout (`api_client.cpp:13-47`) and treats `301` as success (`:38`).
- **What:** Use `notion` body `{"page_size":5,"filter":{"property":"Done","checkbox":{"equals":false}}}`, with the property name made configurable. Use GitHub `per_page=30` with a stream scan or an ArduinoJson filter ([A3](#a3-replace-hand-rolled-json-with-arduinojson)). Read responses through `getStreamPtr()` instead of `getString()`. Log `ESP.getMaxAllocHeap()` before and after each request.
- **Effort:** S. **Risk:** Low.

### R3. Time: POSIX TZ and persisted day
- **Why:** K5, K22. `applyTimezone()` (`calendar_service.cpp:43`) reimplements libc.
- **What:** Store a POSIX TZ string (for example `CET-1CEST,M3.5.0,M10.5.0/3`) and call `configTzTime(tz, "pool.ntp.org")`. In the portal, offer a dropdown of about 40 common zones. For iCal `Z` times, use `timegm()` → `localtime_r()`. Persist an epoch-day number for the rollover (K5).
- **Effort:** S–M. **Risk:** Low. Host-testable ([T1](#t1-host-side-unit-tests-native-env)).

### R4. Keep the last good data on failed refreshes
- **Why:** Calendar and Notion clear the model before fetching (`calendar_service.cpp:278-284`, `notion_service.cpp:36-37`), so one failed fetch blanks the Agenda until the next success. Notion also sets `lastUpdate` before the request (`:33`), so a failure waits the full 10 minutes.
- **What:** Parse into a staging buffer and swap it in only on success. Retry with backoff, as Calendar already does.
- **Effort:** S. **Risk:** Low.

### R5. Crash diagnostics
- **Why:** The `coredump` partition already exists (`min_spiffs.csv`, 64 KB), and `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`, but nothing reads it.
- **What:** At boot, log `esp_reset_reason()` and the boot count. Add a portal `GET /diag` that returns version, reset reason, uptime, free and min heap, and a `/coredump` download. Add a "Report a bug" note to the issue template.
- **Effort:** S–M. **Risk:** Low.

---

## Architecture / Maintainability

### A1. Split `ui.cpp` per screen
- **Why:** `ui.cpp` has 2,157 lines and 28 `draw*` functions with one `fillScreen` each. `TaskyUI` holds the state of every screen (`ui.h:84-114`). `ui.cpp` reaches back into `tasky.ino` globals through `extern` (`ui.cpp:7-10`).
- **What (recommended):** Define a small `Screen` interface. Put each screen in its own file with its own state, and keep a shared `gfx` helper for `centerTextFont`, `fitText`, `drawRow` and the theme.
  ```cpp
  struct View { virtual void enter() = 0; virtual void onInput(const InputEvent&) = 0;
                virtual void tick(uint32_t now) {} virtual ~View() = default; };
  ```
  **Arduino IDE caveat:** the IDE compiles subfolders only under `src/`. Use `tasky/src/ui/…` and `tasky/src/core/…` so `tasky.ino` keeps opening in the IDE. PlatformIO (`src_dir = tasky`) compiles recursively either way.
- **Effort:** M–L (do it incrementally, one screen per PR). **Risk:** Low–Med. It is a pure refactor, but each screen needs a visual check on the device.

### A2. Extract input and navigation from `tasky.ino`
- **Why:** `tasky.ino` is 1,356 lines. It mixes HAL (`setLed`, buzzer), raw input decoding (`:143-223`), the navigation and overlay state machine (`:225-285`, `:1248-1300`), 15 screen handlers and the factory reset (`:1222-1246`).
- **What:** Split it into `hal/buzzer_led.{h,cpp}`, `input/input.{h,cpp}` (encoder plus buttons → `InputEvent{type, step}` queue, with debounce, long-press and double-click), and `app/navigator.{h,cpp}` (carousel, More menu, overlays, focus lock). The handlers then move into the A1 views. `tasky.ino` ends up as about 100 lines of wiring. This is also the prerequisite for [U2](#u2-interrupt-driven-encoder).
- **Effort:** M. **Risk:** Med. Test every navigation path on the device.

### A3. Replace hand-rolled JSON with ArduinoJson
- **Why:** Five ad-hoc parsers based on `indexOf`: `notion_service.cpp:62-158`, `spotify_service.cpp:280-330`, `weather_service.cpp:46-90`, `llm_service.cpp:8-22,507-573`, `github_service.cpp:58`. None of them decodes escapes (`\"`, `\uXXXX`). Key collisions such as GitHub's nested `created_at` give wrong results. Each `substring` copies data on the heap. Building requests by hand caused K16.
- **What:** Use ArduinoJson v7 with stream parsing and a filter, which keeps memory bounded:
  ```cpp
  JsonDocument filter; filter["results"][0]["properties"]["Name"]["title"][0]["plain_text"] = true;
  JsonDocument doc;
  deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  ```
  It costs about 15–25 KB of flash. Use it to serialize request bodies too.
- **Effort:** M. **Risk:** Low–Med. Re-test each integration.

### A4. Split config and persistence
- **Why:** `Config` (`model.h:43-109`) mixes user settings, secrets, daily counters, runtime state (`llmHourStart`) and cache (`nextEventSummary`). `save()` writes about 140 keys, including 84 separate `s0…s83` streak keys, on every task toggle or timer start (`tasky.ino:323,410`).
- **What:** Use separate namespaces: `cfg` (save on change), `sec` (secrets), `day` (daily stats) and `streak` (one `putBytes` blob). Write a one-time migration from the old keys.
- **Effort:** M. **Risk:** Med. Test the upgrade path on a device that already has NVS data.

### A5. Remove dead code
- **Why:**
  - `ble_notifications.h` is never included.
  - `animation.h` is 229 lines, of which only `triggerClick()` is used (`ui.cpp:98`), and its state is never rendered.
  - `TaskyUI::goToScreen()` is a no-op (`ui.cpp:83`).
  - `CalendarService::processEvent` is declared but never defined (`calendar_service.h:61`).
  - `clockEditing` and `timerEditing` are unused (`ui.h:94-96`).
  - `daysInMonth` is unused (`tasky.ino:435`).
  - The `TASKY_MODE_DEV` dual portal duplicates UI paths.
  - Spotify and Firebase are off by default. The Firebase guide recommends world-writable rules (`wifi_portal.cpp:330`).
- **What:** Delete the unused code. Decide whether Spotify and Firebase stay in the repo (an `experimental/` folder) or go.
- **Effort:** S. **Risk:** Low.

### A6. One place for hardware config
- **Why:** `DISPLAY_CIRCULAR` lives in `ui.h:10`, not in `config_hardware.h`. `ui.cpp:13` hard-codes `W=240,H=240` while `SCREEN_HEIGHT` can be 280 (`config_hardware.h:42`). `config_features.h` uses unconditional `#define`s, so CI can't override them.
- **What:** Move all hardware switches into `config_hardware.h`, wrapped in `#ifndef`, so PlatformIO envs can set `-D DISPLAY_CIRCULAR=1` (plus the GC9A01A lib, K14). Derive `W`/`H` from the config.
- **Effort:** S. **Risk:** Low.

---

## Testing

### T1. Host-side unit tests (`native` env)
- **Why:** The riskiest logic is pure computation, and none of it is tested: iCal parsing and RRULE expansion (K18, K19), TZ conversion (K22), the day rollover (K5), streak math (`model.cpp:324-484`) and JSON escaping (K16). Today that logic cannot compile on a host. `parseICalLine` and the helpers are `static`, use `Serial`, and write into `TaskyModel`, which pulls in `Arduino.h` and `Preferences.h`.
- **What:**
  1. Extract Arduino-free modules into `tasky/src/core/`: `ical_parser` (emits occurrences through a callback), `time_util` (days in month, day of week, epoch-day, TZ), `streak` and `json_escape`.
  2. Add a native env:
     ```ini
     [env:native]
     platform = native
     test_framework = unity
     build_src_filter = -<*> +<src/core/>
     build_flags = -std=gnu++17 -DUNIT_TEST
     ```
  3. Write fixtures under `test/fixtures/*.ics`: a Google export (UTC, TZID and all-day events), `WEEKLY;BYDAY=MO,WE;COUNT=6`, `EXDATE`, folded lines, CRLF line endings, leap year, DST boundary and month wrap. Add streak wrap-around and `hhmmToMinutes` edge cases.
- **Effort:** M (most of the work is the extraction). **Risk:** Low. No hardware needed.

### T2. Release smoke checklist
- **Why:** Display, encoder, buzzer, captive portal and OTA can only be checked on hardware.
- **What:** Write `docs/TESTING.md` with a 15-minute checklist per release and per chip: flash from the web, portal on iOS and Android, OTA upload and rollback, each screen, Wi‑Fi loss and recovery, and a midnight rollover (with a fake time via a debug flag). Optionally add Wokwi CI later for boot and serial smoke tests.
- **Effort:** S. **Risk:** Low.

---

## Performance / Memory

### P1. Portal page generation
- **Why:** `htmlPage()` (`wifi_portal.cpp:105-565`) builds about 16.5 KB of literals plus values with about 300 `String +=` calls and no `reserve()`. Reallocation fragments the heap and roughly doubles peak usage, and this runs on every captive-portal probe (`:58`).
- **What:** Serve a static gzipped page from PROGMEM with `send_P`. Load and save settings through `GET/POST /api/config` as small JSON without secrets. That also makes i18n ([U1](#u1-i18n-and-non-ascii-text)) a client-side string table. Minimal alternative: `setContentLength(CONTENT_LENGTH_UNKNOWN)` plus `sendContent()` in chunks.
- **Effort:** S (chunked) to M (static and API). **Risk:** Low.

### P2. Display redraw cost
- **Why:** Every screen clears with a full `fillScreen` (28 call sites). At the 10 MHz SPI clock chosen for stability (`ui.cpp:44`), that's 115 KB per clear, or about 90 ms with visible flicker. The Agent screen does a full redraw every 400 ms (`tasky.ino:950`) and Spotify every 1 s (`:899`).
- **What:** Redraw only the changed regions, as the timer already does. For text-heavy areas, use a small `GFXcanvas16` such as 184×48 (about 17 KB). A full-screen canvas is too large for a C3. Re-test SPI at 20–40 MHz with the final wiring.
- **Effort:** M. **Risk:** Low. Check visually.

### P3. `String` in hot paths (honest assessment)
- **Why:** The timer path is already fine. The remaining churn is in the portal (P1), LLM context building (`llm_service.cpp:390-465`, one-off), `fitText` (`ui.cpp:934-948`, short strings) and the `handleWifi` status compare every loop (`tasky.ino:569-573`).
- **What:** Fix the portal and leave the rest alone unless heap telemetry ([R5](#r5-crash-diagnostics)) shows otherwise.
- **Effort:** S. **Risk:** Low.

### P4. Flash budget and partitions
- **Why:** The slot is 82 % full. S3 (about 65 KB) plus A3 (about 20 KB) would bring it to about 87 %. The 128 KB `spiffs` partition is unused.
- **What:** Add a size gate in CI ([C3](#c3-size-report-on-prs)). If you need room, use a custom `partitions.csv` that drops `spiffs` and grows both app slots. **Note:** OTA can't change the partition table, so existing devices would need one USB or web reflash. Bundle that with a major version.
- **Effort:** S. **Risk:** Med (requires a reflash).

---

## UX / Product

### U1. i18n and non-ASCII text
- **Why:** K23. The portal copy is in Spanish (`wifi_portal.cpp:159-560`). LLM prompts, the system prompt ("Responde SOLO en espanol", `llm_service.cpp:458`) and the offline fallbacks (`:101-111`, with US-dollar prices) are in Spanish. The device UI and docs are in English. The ASCII-only GFX fonts can't display á, ñ or ü from LLM replies, calendar entries or Notion titles.
- **What:** Make English the default. Add `strings_en.h`/`strings_es.h`, selected by a portal setting, and in the portal a client-side table (P1). Ask the LLM to reply in the configured language. Fold UTF-8 to ASCII for display, for example `á→a`, `ñ→n`, `¿→?`. A custom Latin‑1 font is the alternative, at about 10–30 KB per font.
- **Effort:** M. **Risk:** Low.

### U2. Interrupt-driven encoder
- **Why:** `encoderStep()` polls the CLK falling edge once per loop (`tasky.ino:143-153`), so fast turns and any blocking work (R1) lose steps. The buttons are polled too.
- **What:** Use a GPIO ISR on both encoder pins with a quadrature state table and an atomic counter (`IRAM_ATTR`). The **ESP32‑C3 has no PCNT peripheral** (the C6 does), so one ISR implementation covers both chips. Keep button decoding in the input module (A2), sampled from a 1 ms `esp_timer`.
  ```cpp
  static volatile int32_t encCount; static uint8_t encState;
  void IRAM_ATTR encIsr() {
    static const int8_t T[16] = {0,-1,1,0, 1,0,0,-1, -1,0,0,1, 0,1,-1,0};
    encState = (encState << 2 | digitalRead(ENCODER_CLK) << 1 | digitalRead(ENCODER_DT)) & 0x0F;
    encCount += T[encState];   // /4 per detent (or /2, depends on encoder)
  }
  ```
- **Effort:** S–M. **Risk:** Med. Bounce characteristics vary, so test on both boards.

### U3. Power
- **Why:** This is a USB desk device. Deep sleep would break the timer, meeting alerts and Wi‑Fi freshness: reconnecting plus TLS takes 2–4 s per wake. `WIFI_AP_STA` (K25) prevents modem sleep. The backlight is on all the time (K7).
- **What:**
  - Use `WIFI_STA` outside the portal, which re-enables the default modem sleep.
  - Add a night mode that turns the backlight off in Ambient outside work hours. This needs a controllable backlight pin.
  - If a battery is fitted, light-sleep in Ambient with GPIO wake on the encoder or MODE button, and read the battery voltage.
  - Deep sleep only for an explicit "off" mode.
- **Effort:** M. **Risk:** Med (needs hardware).

### U4. Setup and agent polish
- **Why:**
  - The time zone is a bare number field (`wifi_portal.cpp:250`, no server-side clamp at `:664`).
  - GitHub token and Notion fields are missing in user mode (K21).
  - The LLM model IDs are hard-coded (`llm_service.cpp:479,496`).
  - The agent adds a fake 800 ms delay (`tasky.ino:998`).
  - The calendar day cursor wraps 1–31 regardless of month (`tasky.ino:437-441`, clamped later in `ui.cpp:557`).
- **What:** Add a TZ dropdown (R3), the missing fields, and a model ID in config. Remove the delay. Wrap the cursor using the real month length.
- **Effort:** S. **Risk:** Low.

---

## Tooling / CI

The workflows `ci.yml` and `release.yml` already build the C3 and C6 matrix, cache `~/.platformio`, write the app and merged sizes to the step summary, check the tag against `version.h`, and publish merged, OTA, parts and `SHA256SUMS`. All of that is good. The proposals below add to it.

### C1. Reproducible builds: pin `lib_deps`
- **Why:** `platformio.ini:26-29` pulls the latest Adafruit libs on every cold build. A tag rebuilt next month can differ. `SD` and `Adafruit seesaw` are pulled in transitively (`.pio/libdeps/`) without being used.
- **What:**
  ```ini
  lib_deps =
      adafruit/Adafruit GFX Library @ 1.12.1
      adafruit/Adafruit ST7735 and ST7789 Library @ 1.11.0
      adafruit/Adafruit BusIO @ 1.17.0
  lib_ignore = SD, Adafruit seesaw Library
  ```
  The versions shown are examples; use whatever `.pio/libdeps/*/` resolves to today. Add Dependabot for `github-actions`.
- **Effort:** S. **Risk:** Low.

### C2. CI ergonomics
- **What:**
  - `concurrency: { group: ci-${{ github.ref }}, cancel-in-progress: true }`.
  - `setup-python` with `cache: pip`.
  - Pin `platformio==6.1.x` instead of `pip install -U`.
  - Include `firmware/tasky/**` and `web/**` changes in the path filters as needed.
  - A `native` test job (T1).
  - A feature-flag matrix: all `FEATURE_*` set to 0, and Spotify plus Firebase set to 1, so disabled code does not bit-rot. This requires the `#ifndef` change from A6.
- **Effort:** S. **Risk:** Low.

### C3. Size report on PRs
- **Why:** The slot is 82 % full (P4). Today the sizes are only in the run summary.
- **What:** In a PR, build the base and head, or download the base size artifact from `main`. Post a sticky comment and fail above 95 % of 0x1E0000.
  ```yaml
  - run: |
      APP=$(stat -c%s firmware/.pio/build/$E/firmware.bin); SLOT=1966080
      echo "| $E | $APP | $((APP*100/SLOT))% |" >> size.md
      [ $APP -lt $((SLOT*95/100)) ]
  - uses: marocchino/sticky-pull-request-comment@v2
    with: { header: size, path: size.md }
  ```
  For a per-symbol breakdown, upload `firmware.map` as an artifact, or run `pio run -t size` and `esp-idf-size`.
- **Effort:** S–M. **Risk:** Low.

### C4. Static analysis and formatting
- **What:**
  - `pio check -e xiao_c3 --skip-packages --severity=high --fail-on-defect=high` (cppcheck).
  - A `.clang-format` matching the current 2-space LLVM-like style. Do one formatting commit, list it in `.git-blame-ignore-revs`, then run `clang-format --dry-run -Werror` on changed files in CI.
  - Optionally, `clang-tidy` on the `native` core modules.
- **Effort:** S. **Risk:** Low.

### C5. Release provenance
- **What:** Run `actions/attest-build-provenance` on the release binaries; it is free and lets users verify which workflow built them. Sign the OTA hash for S5. Add the app size and percentage to the release body.
- **Effort:** S. **Risk:** Low.

---

## Web flasher

The current state is good. `fetch-firmware.mjs` pulls the latest release at build time, serves it same-origin (which works around the missing CORS headers), and flashes the individual parts so NVS survives a reinstall.

### W1. One manifest, automatic chip selection
- **Why:** The page asks you to pick a board (`web/public/app.js:5`, `manifest-c3.json`/`manifest-c6.json`). Picking the wrong board makes the flash fail with a confusing error.
- **What:** ESP Web Tools picks the `builds[]` entry whose `chipFamily` matches the connected chip, so write one `manifest.json` with both builds and drop the radio buttons, or keep them as information only.
  ```json
  { "name": "Tasky", "version": "2.4.0", "new_install_prompt_erase": true,
    "builds": [ { "chipFamily": "ESP32-C3", "parts": [/* c3 parts */] },
                { "chipFamily": "ESP32-C6", "parts": [/* c6 parts */] } ] }
  ```
  This works as long as only XIAO boards are supported. A different C3 board would need its own manifest.
- **Effort:** S. **Risk:** Low.

### W2. Improv Wi‑Fi over serial
- **Why:** After flashing, users must join the `Tasky` AP and use the portal. ESP Web Tools probes for Improv right after install and can collect Wi‑Fi credentials in the browser.
- **What:** Implement the Improv serial protocol (`improv-wifi/sdk-cpp`, MIT, small) in the input loop over `Serial` (USB-CDC on both chips).
  1. Handle `WIFI_SETTINGS`: store `cfg.wifiSSID/wifiPass`, then call `net.begin()`.
  2. Reply `PROVISIONED` with `http://<ip>/` as the redirect URL.
  3. Answer `GET_DEVICE_INFO` with `Tasky`, `TASKY_VERSION`, the chip and `Tasky`.

  Improv frames start with `IMPROV`, so existing log output can coexist. Add `"new_install_improv_wait_time": 15` to the manifest, because boot includes a 1.3 s splash and `delay(500)` ×2 (`tasky.ino:1075-1077,1121`). With S2, this also removes the default-AP exposure for most users.
- **Effort:** M. **Risk:** Med. On USB-CDC, the port re-enumerates after reset. Test on both chips with Chrome on macOS, Windows and Linux.

### W3. Beta release channel
- **Why:** `releases/latest` skips pre-releases, so testers have no way to get beta builds from the flasher.
- **What:** `fetch-firmware.mjs` also selects the newest `prerelease: true` release from `/releases?per_page=10` and writes `firmware/beta/manifest.json` plus `latest-beta.json`. Add a "Beta" toggle (or `?channel=beta`) to the page. Tag betas as `v2.5.0-beta.1`. The tag check in `release.yml` already compares against `version.h`.
- **Effort:** S. **Risk:** Low.

### W4. Supply chain and integrity
- **Why:** `index.html:18` loads `https://unpkg.com/esp-web-tools@10/...`, a floating major version from a third-party CDN, into a page that writes firmware to the user's device. `fetch-firmware.mjs` doesn't check the downloads against `SHA256SUMS`.
- **What:** Pin an exact version, ideally vendored in `web/public/vendor/` or loaded from jsDelivr with SRI. Verify every part against `SHA256SUMS` at build time and fail the build on a mismatch. Add a CSP header in `vercel.json`.
- **Effort:** S. **Risk:** Low.
