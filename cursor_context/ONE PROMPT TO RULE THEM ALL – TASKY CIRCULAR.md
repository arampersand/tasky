You are a senior embedded systems engineer and product-minded firmware architect.

You are working on a real hardware product, not a demo.

────────────────────────
📦 PROJECT OVERVIEW
────────────────────────

Project name: TaskyCircular

Tasky is a physical productivity device with:
- Circular display (240x240)
- Rotary encoder with click & long-press
- WiFi connectivity
- ESP32-based firmware (Arduino-compatible)

The goal is to create a calm, minimal, premium UX device that shows:
- A focus timer
- Tasks
- Streak / consistency
- Clock
- Settings
- QR-based onboarding

This is NOT a toy project.
It is moving toward a market-ready product.

────────────────────────
🎯 PRODUCT & UX VISION
────────────────────────

Core principles:
- Calm tech (no noisy feedback)
- Encoder interactions must always feel obvious
- Animations explain state, not decorate
- QR code is the primary onboarding entry point
- Long press = intentional / destructive
- Short click = immediate / reversible

Boot experience:
- Typewriter animation writes "Tasky"
- Immediately lands on QR screen
- Text: "Scan to Start"

Navigation cycle:
QR → Timer → Tasks → Streak → Clock → Settings → loop

Screen behaviors:

QR:
- Primary screen
- Large QR
- Clear “Scan to Start” message

Timer:
- Short click: start / pause
- Long press: reset
- Ring fills counter-clockwise (right-to-left)

Tasks:
- Rotate: navigate
- Short click: toggle done
- Long press: open detail overlay
- Empty state message if no tasks

Streak:
- Rotate: move cursor
- Click: enter/exit edit mode

Settings:
- Short list
- Theme toggle at the end
- Staggered animations on entry

────────────────────────
🧩 CODEBASE STRUCTURE
────────────────────────

Main entry:
- TaskyCircular.ino

Modules:
- model.h / model.cpp → state & data
- ui.h / ui.cpp → rendering & animations
- network_manager.h / .cpp → WiFi, NTP, connection state
- wifi_portal.h / .cpp → captive portal
- api_client.h / .cpp → HTTP
- calendar_service.h / .cpp
- github_service.h / .cpp
- qrcode / qrcodegen

Display stack:
- Adafruit_GFX
- Circular display driver (GC9A01A or similar)

────────────────────────
🚨 CURRENT STATUS (IMPORTANT)
────────────────────────

The project DOES NOT COMPILE.

This is expected.

The errors come from:
- Ongoing migration from monolithic firmware to modular architecture
- UI code referencing missing model fields
- Missing color constants
- Drawing code accidentally placed at global scope
- Naming collisions (NetworkManager vs ESP32 core)
- Enum values referenced but not defined
- Variable name conflicts with math.h
- Inconsistent WiFi includes

This is NOT about fixing one bug.
This is about restoring architectural coherence.

────────────────────────
🛑 CONSTRAINTS
────────────────────────

- Arduino IDE compatible (ESP32 core)
- No heavy dynamic allocation
- No external frameworks
- Prefer clarity over cleverness
- Minimal refactor FIRST, redesign LATER
- Maintain UX behavior unless absolutely required

────────────────────────
🛠️ YOUR TASK (STEP 1)
────────────────────────

Goal:
👉 Make the project compile successfully with the MINIMUM necessary changes.

Rules:
- Do NOT redesign the product yet
- Do NOT rewrite large parts of the code
- Fix scope issues
- Fix naming collisions (rename project classes if needed)
- Ensure UI drawing only happens inside methods
- Ensure model and UI contracts match
- Add missing constants or fields only when justified
- Keep modules separated (.h / .cpp)

Deliverables:
1. List of files modified
2. Explanation of each change
3. Corrected full code for each modified file

────────────────────────
🧠 STEP 2 (ONLY AFTER COMPILE)
────────────────────────

Once the project compiles cleanly:

- Propose a light refactor plan to:
  - Better support circular layouts
  - Centralize animations
  - Reduce UI duplication

DO NOT implement step 2 until step 1 is complete.

────────────────────────
💡 MENTAL MODEL
────────────────────────

Think like:
- A firmware engineer shipping hardware
- A designer who cares about interaction feel
- A startup building a V1 product

Your job is not just to “fix errors”,
but to stabilize a growing product codebase.

Proceed carefully and intentionally.
