# Current Blocker (DO NOT IGNORE)

The project does NOT compile.

Main issues observed:
- Missing color constants (C_PRIMARY, C_DIM)
- UI references model fields that no longer exist
- Drawing code exists outside of functions (global scope)
- Naming collision: NetworkManager conflicts with ESP32 core
- Enum values referenced but not defined
- Variable naming conflicts with math.h (y0)
- Inconsistent WiFi includes

Goal:
- Make the project compile
- Preserve UX behavior
- Minimal refactor, not rewrite
