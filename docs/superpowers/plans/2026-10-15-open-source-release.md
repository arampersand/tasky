# Tasky Open-Source Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish Tasky as the public GPL-3.0 monorepo `arampersand/tasky` with docs, CI, tagged releases and a browser flasher at `tasky-flash.vercel.app` that always serves the latest release.

**Architecture:** Firmware stays PlatformIO (pioarduino platform, envs `xiao_c3`/`xiao_c6`) and gains a post-build script that emits a single merged image per board. A tag triggers a GitHub Actions release that uploads the merged images and pings a Vercel deploy hook; the Vercel build downloads the latest release into `web/public/firmware/` so ESP Web Tools fetches same-origin (GitHub release assets have no CORS).

**Tech Stack:** PlatformIO 6 + pioarduino `platform-espressif32` 55.03.33, esptool `merge_bin`, GitHub Actions, `softprops/action-gh-release`, ESP Web Tools 10 (CDN), static HTML/CSS/JS, Node 20 build script, Vercel CLI, `gh`, `git-filter-repo`.

**Spec:** `docs/superpowers/specs/2026-10-15-open-source-release-design.md`

**Environment caveats (read first):**
- The working copy lives at `/Volumes/&/tasky/TaskyFirmware`. The `&` breaks PlatformIO builds (include paths go through `sh`). Build via the ampersand-free symlinks: `S=/private/tmp/claude-501/-Volumes---tasky/89f5ee3b-aff5-4a07-a5dc-e4ac8c18203c/scratchpad; cd $S/proj/<firmware dir> && PLATFORMIO_CORE_DIR=$S/core $S/core/penv/bin/pio run -e xiao_c3`. `$S/proj` → repo root, `$S/core` → `/Volumes/&/tasky/.pio-core-v2` (has the pioarduino packages cached and a Python 3.13 penv; the Homebrew `pio` uses Python 3.14 which pioarduino rejects).
- Git pushes must go over SSH (`git@github.com:...`): the HTTPS keychain token lacks the `workflow` scope needed to push `.github/workflows/*`.
- Commits end with the attribution trailer given in the session.

---

### Task 1: Purge history and force-push

**Goal:** History contains no `backup/`, vendored Adafruit libs, `.vscode`, `.DS_Store`; remote `main` is rewritten.

**Files:**
- Delete (all history): `backup/`, `docs/Adafruit_BusIO/`, `docs/Adafruit_GC9A01A/`, `docs/Adafruit_GFX_Library/`, `docs/Adafruit_ST7735_and_ST7789_Library/`, `.vscode/`, every `.DS_Store`, `TaskyCircular/qrcode/`
- Delete (working tree): `.pio-build.log`, `.pio-flash.log`

**Acceptance Criteria:**
- [ ] Mirror backup exists at `~/tasky-backup-2026-10-15.git` (already created, 19 MB)
- [ ] `git rev-list --all --objects | grep -ciE "ical|backup/feedback|Adafruit_|\.DS_Store|\.vscode"` → `0`
- [ ] `git log --oneline | wc -l` → 15 (13 original + 2 new) — history preserved
- [ ] `git push --force origin main` succeeds; `gh api repos/arampersand/TaskyFirmware/git/trees/main?recursive=1 --jq '.tree[].path' | grep -c backup` → `0`

**Verify:** the two `grep -c` commands above print `0`.

**Steps:**

- [ ] **Step 1: Rewrite history** (filter-repo refuses to run on a non-fresh clone unless `--force`; we have the mirror backup)

```bash
cd "/Volumes/&/tasky/TaskyFirmware"
rm -f .pio-build.log .pio-flash.log
git filter-repo --force --invert-paths \
  --path backup/ \
  --path docs/Adafruit_BusIO/ --path docs/Adafruit_GC9A01A/ \
  --path docs/Adafruit_GFX_Library/ --path docs/Adafruit_ST7735_and_ST7789_Library/ \
  --path .vscode/ --path TaskyCircular/qrcode/ \
  --path-glob '*.DS_Store'
git remote add origin git@github.com:arampersand/TaskyFirmware.git   # filter-repo drops remotes
git gc --prune=now --aggressive -q
```

- [ ] **Step 2: Verify purge, then force-push**

```bash
git rev-list --all --objects | grep -ciE "ical|backup/feedback|Adafruit_|\.DS_Store|\.vscode"   # expect 0
git log --oneline | wc -l                                                                     # expect 15
git push --force origin main
```

---

### Task 2: Monorepo layout, version.h, merged-image script

**Goal:** `firmware/` builds both envs from the new layout and each build emits `tasky-<env>-merged.bin`; `web/` and `docs/` skeleton dirs exist.

**Files:**
- Move: `TaskyCircular/` → `firmware/tasky/`, `TaskyCircular/TaskyCircular.ino` → `firmware/tasky/tasky.ino`, `platformio.ini` (untracked) → `firmware/platformio.ini`
- Create: `firmware/tasky/version.h`, `firmware/tasky/config_secrets.h.example`, `firmware/scripts/merge_bin.py`
- Modify: `firmware/tasky/ota_service.cpp:3` (use `TASKY_VERSION`), `firmware/tasky/config_features.h:21-27` (comment points at `.example`), `firmware/platformio.ini`, `.gitignore`

**Acceptance Criteria:**
- [ ] `pio run -e xiao_c3` and `pio run -e xiao_c6` succeed from `firmware/`
- [ ] `.pio/build/xiao_c3/tasky-xiao_c3-merged.bin` exists and is > 1.6 MB; first bytes `e9` (ESP image magic) at offset 0
- [ ] `grep TASKY_VERSION firmware/tasky/ota_service.cpp` matches; `grep '"2.4.0"' firmware/tasky/version.h` matches
- [ ] `git status` clean after commit

**Verify:** `ls -la firmware/.pio/build/xiao_c*/tasky-*-merged.bin && xxd -l 1 firmware/.pio/build/xiao_c3/tasky-xiao_c3-merged.bin` → two files, byte `e9`.

**Steps:**

- [ ] **Step 1: Move files**

```bash
cd "/Volumes/&/tasky/TaskyFirmware"
mkdir -p firmware/scripts web docs
git mv TaskyCircular firmware/tasky
git mv firmware/tasky/TaskyCircular.ino firmware/tasky/tasky.ino
git mv platformio.ini firmware/platformio.ini 2>/dev/null || mv platformio.ini firmware/platformio.ini
```

- [ ] **Step 2: `firmware/tasky/version.h`**

```cpp
#pragma once
// Single source of truth for the firmware version.
// release.yml refuses to publish a tag that does not match this string.
#define TASKY_VERSION "2.4.0"
```

- [ ] **Step 3: `ota_service.cpp` uses it**

Replace line 3 `const char* OtaService::FIRMWARE_VERSION = "2.3.0";` with:
```cpp
#include "version.h"
const char* OtaService::FIRMWARE_VERSION = TASKY_VERSION;
```

- [ ] **Step 4: `firmware/tasky/config_secrets.h.example`** (copy the 16 `SECRET_*` defines from `config_features.h:28-43`, with a header comment "Copy to config_secrets.h and fill in. Never commit config_secrets.h." Values stay empty strings / 0.) Update the comment at `config_features.h:22` to say `Copy config_secrets.h.example to config_secrets.h`.

- [ ] **Step 5: `firmware/platformio.ini`** — change `src_dir = TaskyCircular` → `src_dir = tasky`; add under `[env]`: `extra_scripts = post:scripts/merge_bin.py`; update the header comment paths.

- [ ] **Step 6: `firmware/scripts/merge_bin.py`**

```python
# Post-build: merge bootloader + partitions + boot_app0 + app into one image at
# offset 0x0 so ESP Web Tools / esptool can flash it with a single part.
Import("env")
import os, subprocess

def merge_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    out = os.path.join(build_dir, f"tasky-{env['PIOENV']}-merged.bin")
    board = env.BoardConfig()
    chip = board.get("build.mcu")                          # esp32c3 / esp32c6
    flash_size = board.get("upload.flash_size", "4MB")
    flash_mode = board.get("build.flash_mode", "dio")
    app_offset = env.subst("$ESP32_APP_OFFSET") or "0x10000"
    extra = env.get("FLASH_EXTRA_IMAGES", [])              # [(offset, path), ...] set by the platform
    parts = []
    for off, path in extra:
        parts += [env.subst(off), env.subst(path)]
    parts += [app_offset, os.path.join(build_dir, "firmware.bin")]
    esptool_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")
    cmd = [env.subst("$PYTHONEXE"), os.path.join(esptool_dir, "esptool.py"),
           "--chip", chip, "merge_bin", "-o", out,
           "--flash_mode", flash_mode, "--flash_size", flash_size] + parts
    print("merge_bin:", " ".join(cmd))
    subprocess.check_call(cmd)
    print("Merged image:", out, os.path.getsize(out), "bytes")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
```
If `tool-esptoolpy` ships a binary instead of `esptool.py`, use `[sys.executable, "-m", "esptool", ...]` from the penv (esptool is installed there). Check `ls $(core)/packages/tool-esptoolpy` first.

- [ ] **Step 7: `.gitignore`** (replace whole file)

```
# Secrets — never commit
firmware/tasky/config_secrets.h

# Build output
firmware/.pio/
*.bin
*.elf
*.hex
*.o

# Web build output (fetched from GitHub Releases at build time)
web/public/firmware/
web/.vercel/
node_modules/

# OS / IDE
.DS_Store
.vscode/
.idea/
*.swp
*.log
```

- [ ] **Step 8: Build both envs via symlinks, verify merged bins, commit**

```bash
S=/private/tmp/claude-501/-Volumes---tasky/89f5ee3b-aff5-4a07-a5dc-e4ac8c18203c/scratchpad
cd $S/proj/firmware && for e in xiao_c3 xiao_c6; do PLATFORMIO_CORE_DIR=$S/core $S/core/penv/bin/pio run -e $e || exit 1; done
ls -la .pio/build/xiao_c*/tasky-*-merged.bin && xxd -l 1 .pio/build/xiao_c3/tasky-xiao_c3-merged.bin
cd "/Volumes/&/tasky/TaskyFirmware" && git add -A && git commit -m "chore: monorepo layout, version.h, merged flash image"
```

---

### Task 3: CI and release workflows

**Goal:** Every push/PR builds both boards; a `v*` tag publishes a GitHub Release with two merged images + `SHA256SUMS` and pings Vercel.

**Files:**
- Create: `.github/workflows/ci.yml`, `.github/workflows/release.yml`

**Acceptance Criteria:**
- [ ] `gh run list --commit $(git rev-parse HEAD)` shows `ci` success for both matrix entries
- [ ] `release.yml` fails fast when tag ≠ `TASKY_VERSION`
- [ ] Release assets are named `tasky-xiao_c3-vX.Y.Z.bin`, `tasky-xiao_c6-vX.Y.Z.bin`, `SHA256SUMS`

**Verify:** `gh run watch $(gh run list --commit $(git rev-parse HEAD) --json databaseId --jq '.[0].databaseId') --exit-status` → exit 0.

**Steps:**

- [ ] **Step 1: `.github/workflows/ci.yml`**

```yaml
name: ci
on:
  push:
    branches: [main]
    paths: [firmware/**, .github/workflows/ci.yml]
  pull_request:
    paths: [firmware/**, .github/workflows/ci.yml]
jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        env: [xiao_c3, xiao_c6]
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with: { python-version: "3.11" }
      - uses: actions/cache@v4
        with:
          path: ~/.platformio
          key: pio-${{ runner.os }}-${{ hashFiles('firmware/platformio.ini') }}
      - run: pip install platformio
      - run: pio run -e ${{ matrix.env }}
        working-directory: firmware
      - uses: actions/upload-artifact@v4
        with:
          name: tasky-${{ matrix.env }}
          path: firmware/.pio/build/${{ matrix.env }}/tasky-${{ matrix.env }}-merged.bin
```

- [ ] **Step 2: `.github/workflows/release.yml`**

```yaml
name: release
on:
  push:
    tags: ["v*"]
permissions:
  contents: write
jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        env: [xiao_c3, xiao_c6]
    steps:
      - uses: actions/checkout@v4
      - name: Check tag matches version.h
        run: |
          TAG="${GITHUB_REF_NAME#v}"
          grep -q "\"$TAG\"" firmware/tasky/version.h || { echo "version.h != $TAG"; exit 1; }
      - uses: actions/setup-python@v5
        with: { python-version: "3.11" }
      - uses: actions/cache@v4
        with:
          path: ~/.platformio
          key: pio-${{ runner.os }}-${{ hashFiles('firmware/platformio.ini') }}
      - run: pip install platformio
      - run: pio run -e ${{ matrix.env }}
        working-directory: firmware
      - run: cp firmware/.pio/build/${{ matrix.env }}/tasky-${{ matrix.env }}-merged.bin tasky-${{ matrix.env }}-${{ github.ref_name }}.bin
      - uses: actions/upload-artifact@v4
        with: { name: "${{ matrix.env }}", path: "tasky-${{ matrix.env }}-${{ github.ref_name }}.bin" }
  publish:
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/download-artifact@v4
        with: { merge-multiple: true, path: dist }
      - run: cd dist && sha256sum *.bin > SHA256SUMS && cat SHA256SUMS
      - uses: softprops/action-gh-release@v2
        with:
          files: dist/*
          generate_release_notes: true
      - name: Redeploy flasher site
        if: ${{ env.HOOK != '' }}
        env: { HOOK: "${{ secrets.VERCEL_DEPLOY_HOOK }}" }
        run: curl -fsS -X POST "$HOOK" > /dev/null && echo "Vercel deploy triggered"
```

- [ ] **Step 3: Commit, push over SSH, watch CI**

```bash
git remote set-url origin git@github.com:arampersand/TaskyFirmware.git
git add .github && git commit -m "ci: build matrix and tagged releases" && git push origin main
sleep 20; gh run watch $(gh run list --commit $(git rev-parse HEAD) --json databaseId --jq '.[0].databaseId') --exit-status
```
If CI fails on something environmental (package download, esptool path), fix and re-push; do not merge a red build.

---

### Task 4: Documentation set

**Goal:** Public docs derived from the code; legacy context docs removed.

**Files:**
- Create: `README.md`, `README.es.md`, `LICENSE` (GPL-3.0 text), `CONTRIBUTING.md`, `CHANGELOG.md`, `docs/MANUAL.md`, `docs/HARDWARE.md`, `docs/BUILDING.md`, `docs/CUSTOMIZING.md`, `docs/ARCHITECTURE.md`, `docs/IMPROVEMENTS.md`, `.github/ISSUE_TEMPLATE/bug_report.md`, `.github/ISSUE_TEMPLATE/feature_request.md`, `.github/PULL_REQUEST_TEMPLATE.md`
- Delete: `context/`, `cursor_context/`, `WARP.md`, `docs/SPOTIFY_Y_MODO_DEV.md`

**Sources of truth for writers:** `firmware/tasky/*.h/*.cpp` (screens: `ui.h` `Screen` enum + `tasky.ino` handlers; controls: `tasky.ino` input section; portal routes/forms: `wifi_portal.cpp`; integrations: each `*_service.cpp`; pins: `config_hardware.h`; flags: `config_features.h`; persistence keys: `model.cpp`). Legacy `context/readme.md`, `cursor_context/01_PRODUCT_VISION.md`, `02_UX_WALKTHROUGH.md` only for product voice/pinout table — where they contradict code, code wins.

**Acceptance Criteria:**
- [ ] Every screen in the `Screen` enum has a MANUAL section with encoder rotate / short click / long press / MODO button behaviour
- [ ] HARDWARE pinout matches `config_hardware.h` exactly (D-pin names + GPIO for C3 and C6)
- [ ] CUSTOMIZING covers: feature flags, `DISPLAY_CIRCULAR`, pins, `ROW_OFFSET/COL_OFFSET`, theme colours (cite the palette location in `ui.cpp`), AP SSID/pass (`wifi_portal.h:20-21`), adding a screen (enum + handler + navigation cycle + draw fn), adding a service (pattern of `weather_service.*`), fork + own flasher (fork → tag → Vercel import with root `web`)
- [ ] README has badges (`ci`, release, license), "Flash in 60 seconds" linking `https://tasky-flash.vercel.app`, and links to every doc
- [ ] IMPROVEMENTS lists ≥ 12 proposals, each with Why / What / Effort (S/M/L) / Risk, ordered by value
- [ ] No reference to `TaskyCircular`, Arduino-IDE-only, `EverySecondCounts`, or 7x16 grid remains (`grep -rn` across docs returns nothing)
- [ ] Legacy dirs deleted in the same commit

**Verify:** `grep -rnE "TaskyCircular|EverySecondCounts|7x16|no Makefile" README*.md docs/*.md CONTRIBUTING.md` → empty.

**Steps:**

- [ ] **Step 1: Dispatch three writer subagents in parallel** (each reads the code paths listed above, writes only its files, returns a summary):
  - A: `docs/MANUAL.md`, `docs/HARDWARE.md`, `docs/BUILDING.md`
  - B: `docs/CUSTOMIZING.md`, `docs/ARCHITECTURE.md`, `CONTRIBUTING.md`, issue/PR templates
  - C: `docs/IMPROVEMENTS.md` — audit `ui.cpp` (2157 LOC), `tasky.ino` (1356 LOC), `wifi_portal.cpp`, services; include at least: split `ui.cpp` per screen, extract input handling from `.ino`, host-side `native` env tests for `model` + iCal parser, OTA from GitHub Releases (pull, signed), per-device generated AP password, replace manual JSON parsing with ArduinoJson, watchdog, deep sleep, interrupt-driven encoder, `String` churn in hot paths, portal i18n, HTTPS cert pinning vs `setInsecure()`.
- [ ] **Step 2: Write `README.md` yourself** (hero, badges, features, flash-in-60s, build-it-yourself, make-your-own, architecture summary, roadmap, contributing, license). Then `README.es.md` as a faithful translation. Add `LICENSE` from `https://www.gnu.org/licenses/gpl-3.0.txt`. `CHANGELOG.md` with `## v2.4.0` (from the two commits since e420317) and `## v2.3.0` (from git log).
- [ ] **Step 3: Review every doc against the code** (spot-check 5 claims per doc by opening the referenced lines). Fix inline.
- [ ] **Step 4: Delete legacy docs, run the grep gate, commit**

```bash
git rm -r -q context cursor_context WARP.md docs/SPOTIFY_Y_MODO_DEV.md
grep -rnE "TaskyCircular|EverySecondCounts|7x16|no Makefile" README*.md docs/*.md CONTRIBUTING.md   # expect empty
git add -A && git commit -m "docs: README, manual, hardware, building, customizing, architecture, improvements" && git push origin main
```

---

### Task 5: Web flasher (`web/`)

**Goal:** Static site that fetches the latest release at build time and flashes it via ESP Web Tools.

**Files:**
- Create: `web/vercel.json`, `web/package.json`, `web/scripts/fetch-firmware.mjs`, `web/public/index.html`, `web/public/styles.css`, `web/public/app.js`, `web/README.md`

**Acceptance Criteria:**
- [ ] `node scripts/fetch-firmware.mjs` (with a release present) writes `public/firmware/{tasky-xiao_c3.bin,tasky-xiao_c6.bin,manifest-c3.json,manifest-c6.json,latest.json}`
- [ ] With no release (404) it writes `latest.json` `{ "version": null }` and exits 0, page shows "No release yet"
- [ ] `manifest-c3.json` = `{"name":"Tasky","version":"2.4.0","new_install_prompt_erase":true,"builds":[{"chipFamily":"ESP32-C3","parts":[{"path":"tasky-xiao_c3.bin","offset":0}]}]}` (C6 analogous)
- [ ] Page renders board selector, version, notes, requirements, after-flash steps; `esp-web-install-button` gets `manifest` attribute updated on board change
- [ ] Unsupported browser shows the built-in ESP Web Tools "unsupported" slot text
- [ ] Served locally (`npx serve public`), Playwright confirms `GET /firmware/manifest-c3.json` → 200 and button present

**Verify:** `cd web && node scripts/fetch-firmware.mjs && ls public/firmware && cat public/firmware/manifest-c3.json`.

**Steps:**

- [ ] **Step 1: `web/package.json`**

```json
{
  "name": "tasky-flash",
  "private": true,
  "type": "module",
  "scripts": { "build": "node scripts/fetch-firmware.mjs", "dev": "node scripts/fetch-firmware.mjs && npx serve public" },
  "engines": { "node": ">=20" }
}
```

- [ ] **Step 2: `web/vercel.json`**

```json
{ "buildCommand": "node scripts/fetch-firmware.mjs", "outputDirectory": "public", "framework": null }
```

- [ ] **Step 3: `web/scripts/fetch-firmware.mjs`**

```js
// Downloads the latest GitHub Release of arampersand/tasky into public/firmware/
// and writes ESP Web Tools manifests. Runs at Vercel build time so the .bin files
// are served same-origin (GitHub release assets send no CORS headers).
import { mkdir, writeFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const REPO = process.env.TASKY_REPO ?? "arampersand/tasky";
const OUT = join(dirname(fileURLToPath(import.meta.url)), "..", "public", "firmware");
const BOARDS = [
  { id: "c3", env: "xiao_c3", chipFamily: "ESP32-C3" },
  { id: "c6", env: "xiao_c6", chipFamily: "ESP32-C6" },
];
const headers = { "User-Agent": "tasky-flash-build", Accept: "application/vnd.github+json" };
if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;

await mkdir(OUT, { recursive: true });
const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, { headers });
if (res.status === 404) {
  console.warn("No release yet — writing empty latest.json");
  await writeFile(join(OUT, "latest.json"), JSON.stringify({ version: null }));
  process.exit(0);
}
if (!res.ok) throw new Error(`GitHub API ${res.status}: ${await res.text()}`);
const rel = await res.json();
const version = rel.tag_name.replace(/^v/, "");

for (const b of BOARDS) {
  const asset = rel.assets.find((a) => a.name === `tasky-${b.env}-${rel.tag_name}.bin`);
  if (!asset) throw new Error(`Release ${rel.tag_name} has no asset for ${b.env}`);
  const bin = await fetch(asset.browser_download_url, { headers: { "User-Agent": headers["User-Agent"] } });
  if (!bin.ok) throw new Error(`Download ${asset.name}: ${bin.status}`);
  const file = `tasky-${b.env}.bin`;
  await writeFile(join(OUT, file), Buffer.from(await bin.arrayBuffer()));
  await writeFile(join(OUT, `manifest-${b.id}.json`), JSON.stringify({
    name: "Tasky", version, new_install_prompt_erase: true,
    builds: [{ chipFamily: b.chipFamily, parts: [{ path: file, offset: 0 }] }],
  }, null, 2));
  console.log(`✓ ${file} (${asset.size} bytes) + manifest-${b.id}.json`);
}
await writeFile(join(OUT, "latest.json"), JSON.stringify({
  version, tag: rel.tag_name, name: rel.name, notes: rel.body ?? "",
  publishedAt: rel.published_at, url: rel.html_url,
}, null, 2));
console.log(`Latest release: ${rel.tag_name}`);
```

- [ ] **Step 4: `web/public/index.html` + `styles.css` + `app.js`** — dark theme, Inter via Google Fonts, firmware purple accent (read the palette in `ui.cpp` for the hex), sections per spec. Key markup:

```html
<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
...
<div class="board-select" role="radiogroup">
  <label><input type="radio" name="board" value="c3" checked> XIAO ESP32-C3</label>
  <label><input type="radio" name="board" value="c6"> XIAO ESP32-C6</label>
</div>
<esp-web-install-button id="flash" manifest="/firmware/manifest-c3.json">
  <button slot="activate" class="btn-primary">Flash from your browser</button>
  <span slot="unsupported">Your browser doesn't support Web Serial. Use Chrome or Edge on desktop.</span>
  <span slot="not-allowed">Web Serial needs HTTPS — open this page over https.</span>
</esp-web-install-button>
<p id="version"></p><div id="notes"></div>
```

```js
// app.js
const btn = document.getElementById("flash");
document.querySelectorAll('input[name="board"]').forEach((r) =>
  r.addEventListener("change", () => btn.setAttribute("manifest", `/firmware/manifest-${r.value}.json`)));
const latest = await fetch("/firmware/latest.json").then((r) => r.json()).catch(() => ({ version: null }));
const v = document.getElementById("version");
if (!latest.version) { v.textContent = "No release published yet."; btn.hidden = true; }
else { v.textContent = `Latest firmware: v${latest.version} · ${new Date(latest.publishedAt).toLocaleDateString()}`;
       document.getElementById("notes").textContent = latest.notes; }
```
Use `type="module"` for app.js (top-level await). Release notes are rendered as plain text (no HTML injection).

- [ ] **Step 5: Local test and commit**

```bash
cd "/Volumes/&/tasky/TaskyFirmware/web" && node scripts/fetch-firmware.mjs   # 404 path until Task 7 → "No release yet"
git add -A && git commit -m "feat(web): browser flasher landing page (ESP Web Tools)" && git push origin main
```
Full test with real binaries happens in Task 8 after the first release.

---

### Task 6: Rename and publish the repo

**Goal:** `github.com/arampersand/tasky` is public, licensed, described, with topics and homepage.

**Acceptance Criteria:**
- [ ] `gh repo view arampersand/tasky --json visibility,licenseInfo --jq '[.visibility,.licenseInfo.key]'` → `["PUBLIC","gpl-3.0"]`
- [ ] Local `origin` = `git@github.com:arampersand/tasky.git`; `git fetch` works
- [ ] No doc/workflow references `TaskyFirmware` (`grep -rn TaskyFirmware --exclude-dir=.git .` empty)

**Steps:**

- [ ] **Step 1:**
```bash
gh repo rename tasky --repo arampersand/TaskyFirmware --yes
git remote set-url origin git@github.com:arampersand/tasky.git && git fetch origin
gh repo edit arampersand/tasky --description "Tasky — open-source ESP32 desk companion: Pomodoro, tasks, streaks, calendar. Flash it from your browser." \
  --homepage https://tasky-flash.vercel.app --add-topic esp32,esp32-c3,esp32-c6,xiao,pomodoro,productivity,platformio,arduino,esp-web-tools,open-hardware
grep -rn TaskyFirmware --exclude-dir=.git . ; # fix any hits, commit, push
gh repo edit arampersand/tasky --visibility public --accept-visibility-change-consequences
```

---

### Task 7: First release `v2.4.0`

**Acceptance Criteria:**
- [ ] `gh release view v2.4.0 --json assets --jq '.assets[].name'` → `SHA256SUMS`, `tasky-xiao_c3-v2.4.0.bin`, `tasky-xiao_c6-v2.4.0.bin`
- [ ] `curl -sL <c3 asset url> | head -c1 | xxd` → `e9`

**Steps:**
```bash
git tag -a v2.4.0 -m "v2.4.0 — first open-source release" && git push origin v2.4.0
gh run watch $(gh run list --workflow release --limit 1 --json databaseId --jq '.[0].databaseId') --exit-status
gh release view v2.4.0 --json assets --jq '.assets[].name'
```

---

### Task 8: Vercel project `tasky-flash`

**Goal:** `https://tasky-flash.vercel.app` serves the page with v2.4.0 manifests; releases auto-redeploy.

**Acceptance Criteria:**
- [ ] `curl -sI https://tasky-flash.vercel.app/firmware/manifest-c3.json` → 200; `.../tasky-xiao_c3.bin` → 200 with `content-length` ≈ 1.6 MB
- [ ] Vercel project linked to GitHub `arampersand/tasky`, root directory `web`, production branch `main`
- [ ] Deploy hook URL stored as repo secret `VERCEL_DEPLOY_HOOK` (`gh secret list` shows it)
- [ ] Playwright: page title contains "Tasky", `esp-web-install-button` present, no console errors

**Steps:**
```bash
cd "/Volumes/&/tasky/TaskyFirmware/web"
vercel link --yes --project tasky-flash            # creates the project under zigmods-projects
vercel git connect --yes                           # links GitHub repo; confirm Root Directory = web (set via dashboard/API if CLI didn't)
vercel deploy --prod --yes
```
Create the deploy hook (try the REST API with the CLI token from `~/Library/Application Support/com.vercel.cli/auth.json`: `POST https://api.vercel.com/v1/projects/<id>/deploy-hooks {"name":"release","ref":"main"}`; if unavailable, ask the maintainer to create it in Project → Settings → Git → Deploy Hooks and paste the URL). Then:
```bash
gh secret set VERCEL_DEPLOY_HOOK --repo arampersand/tasky --body "<hook url>"
curl -fsS -X POST "<hook url>"   # trigger a rebuild now that the release exists
```
Verify with curl + Playwright (`mcp__playwright__browser_navigate` to the URL, snapshot, console messages).

---

### Task 9: Hardware verification and wrap-up (maintainer)

- [ ] Maintainer opens `https://tasky-flash.vercel.app` in Chrome, picks the board, flashes a real Tasky, confirms boot + portal.
- [ ] Record outcome in `CHANGELOG.md` if anything changed; save a project memory with the `&`-path and penv build caveats.
