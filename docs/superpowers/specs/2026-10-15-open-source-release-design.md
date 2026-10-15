# Tasky open-source release — design

Date: 2026-10-15. Approved by the maintainer in session.

## Goal

Publish Tasky (ESP32-C3/C6 productivity device firmware) as a public GPL-3.0
monorepo `arampersand/tasky` with: a user/maker manual, a README that sells the
project, a "make your own Tasky" guide, CI that builds both targets, automatic
GitHub Releases on tags, and a static site on Vercel (`tasky-flash.vercel.app`)
that flashes the latest release from the browser via Web Serial.

## Decisions (maintainer)

| Topic | Decision |
|---|---|
| History | Rewrite with `git filter-repo` to purge `backup/` (third-party PII + 20 MB images), vendored `docs/Adafruit_*`, `.vscode/`, `.DS_Store`, logs. Mirror backup first. Force-push. |
| Repo | Rename `TaskyFirmware` → `tasky`; monorepo `firmware/` + `web/` + `docs/`. |
| License | GPL-3.0. |
| Docs language | English; `README.es.md` translation. |
| Web | Landing + flasher, static HTML/CSS/JS, `tasky-flash.vercel.app`. |
| Code improvements | Implement low-risk infra now; larger refactors as prioritized proposals in `docs/IMPROVEMENTS.md`. |
| WIP refactor (2 Oct, uncommitted) | Commit it; it compiles for C3 and C6. Becomes v2.4.0. |

## Repository layout

```
tasky/
├── firmware/
│   ├── platformio.ini            src_dir = tasky; envs xiao_c3, xiao_c6; extra_scripts = scripts/merge_bin.py
│   ├── scripts/merge_bin.py      post-build: bootloader+partitions+boot_app0+app → tasky-<env>-merged.bin @0x0
│   └── tasky/                    sketch folder (tasky.ino opens in Arduino IDE too)
│       ├── version.h             single source of FIRMWARE_VERSION
│       └── config_secrets.h.example
├── web/                          Vercel project, root dir = web
│   ├── public/index.html, styles.css, app.js
│   ├── scripts/fetch-firmware.mjs
│   └── vercel.json
├── docs/MANUAL.md HARDWARE.md BUILDING.md CUSTOMIZING.md ARCHITECTURE.md IMPROVEMENTS.md
├── .github/workflows/ci.yml release.yml, ISSUE_TEMPLATE/, PULL_REQUEST_TEMPLATE.md
└── README.md README.es.md LICENSE CONTRIBUTING.md CHANGELOG.md .gitignore
```

Removed from HEAD and history: `backup/`, `docs/Adafruit_BusIO`, `docs/Adafruit_GC9A01A`,
`docs/Adafruit_GFX_Library`, `docs/Adafruit_ST7735_and_ST7789_Library`, `.vscode/`,
`.DS_Store`, `.pio-*.log`. Removed from HEAD only (content absorbed into new docs first):
`context/`, `cursor_context/`, `WARP.md`, `docs/SPOTIFY_Y_MODO_DEV.md`.

## Latest-image pipeline

1. `git tag vX.Y.Z && git push --tags`.
2. `release.yml`: checks `version.h` == tag, builds `xiao_c3` and `xiao_c6` with
   PlatformIO (pioarduino platform pinned in `platformio.ini`), produces merged
   bins, uploads `tasky-xiao_c3-vX.Y.Z.bin`, `tasky-xiao_c6-vX.Y.Z.bin`,
   `SHA256SUMS` to a GitHub Release, then `POST`s the Vercel deploy hook
   (`VERCEL_DEPLOY_HOOK` repo secret).
3. Vercel build runs `node scripts/fetch-firmware.mjs`: reads
   `api.github.com/repos/arampersand/tasky/releases/latest`, downloads the two
   bins into `public/firmware/`, writes `manifest-c3.json`, `manifest-c6.json`
   (ESP Web Tools format, one part at offset 0, chipFamily ESP32-C3/ESP32-C6)
   and `latest.json` (version, tag, notes, date).
4. Page serves everything same-origin, which sidesteps the missing CORS headers
   on GitHub release assets (verified: `release-assets.githubusercontent.com`
   returns no `access-control-allow-origin`).

Fallback if the hook cannot be created via CLI: maintainer creates it in the
Vercel dashboard and we store it with `gh secret set`.

## Web page

Static, dark theme using the firmware's purple, Inter font, ESP Web Tools from
CDN. Sections: hero + "Flash from your browser" CTA, board selector (C3 / C6),
current version + release notes, what Tasky is (screens), requirements
(Chrome/Edge, USB-C data cable, hold BOOT while plugging in if needed),
after-flash steps (AP `Tasky` / `GetItDone` / `192.168.4.1`), links to docs and
repo. Graceful message when Web Serial is unsupported.

## Documentation

Derived from the current code, not from the stale `context/` docs (they
contradict the code on PlatformIO support, AP password and streak grid).

- README: badges, hero, features, flash-in-60s, build-it-yourself, make-your-own,
  architecture summary, roadmap, contributing, license.
- MANUAL: every screen and control, WiFi portal, integrations (GitHub, iCal,
  Notion, Weather, LLM; Spotify/Firebase experimental and off by default), OTA
  via portal, troubleshooting.
- HARDWARE: BOM, pinout, wiring, display variants (ST7789 square / GC9A01A round).
- BUILDING: PlatformIO CLI/VS Code and Arduino IDE, secrets file, the `&`-in-path
  and Homebrew-pio/Python-3.14 caveats.
- CUSTOMIZING: feature flags, display switch, pins, offsets, theme/colours,
  strings, adding a screen, adding a service, AP credentials, publishing your own
  fork + flasher.
- ARCHITECTURE: layers, data flow, persistence, timing model.
- IMPROVEMENTS: prioritized proposals with effort/risk.

## Execution order and gates

1. Mirror backup → commit WIP → filter-repo → verify purge → force-push.
2. Restructure, version.h, secrets example, cleanup. Gate: both envs compile.
3. Docs (parallel subagents, reviewed against code).
4. CI + release workflows pushed over SSH. Gate: CI green on C3 and C6.
5. Rename repo, license, topics, description → public.
6. Tag v2.4.0. Gate: Release with two merged bins + sums.
7. Vercel project `tasky-flash`, root `web`, hook, deploy. Gate: page loads,
   manifests and bins return 200 same-origin.
8. Maintainer flashes a real device from Chrome (only step needing hardware).
