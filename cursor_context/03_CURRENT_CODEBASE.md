# Codebase Overview

Main entry:
- TaskyCircular.ino

Core modules:
- model.h / model.cpp (estado + persistencia)
- ui.h / ui.cpp (render + animaciones)
- network_manager.h / .cpp (WiFi, NTP, estado)
- wifi_portal.h / .cpp (captive portal)
- api_client.h / .cpp (HTTP)
- github_service.h / .cpp
- calendar_service.h / .cpp
- qrcode / qrcodegen

Display stack:
- Adafruit_GFX
- Circular display driver (GC9A01A or similar)
