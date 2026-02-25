#pragma once

// ==================== TASKY MODE ====================
// 0 = User mode: interfaz simplificada, sin API keys (par usuario promedio)
// 1 = Dev mode: configuracion completa, API keys visibles (ramas dev)
#define TASKY_MODE_DEV 0

// ==================== FEATURE FLAGS (for firmware size reduction) ====================
// Set to 1 to enable, 0 to disable. Disabled features are excluded from compilation.
// Config: Huge APP (3MB) - features esenciales para asistente de productividad.

#define FEATURE_WEATHER  1   // Clima - contexto del dia
#define FEATURE_SPOTIFY  0   // Spotify - disabled until BLE music from phone app is ready
#define FEATURE_FIREBASE 0   // Cloud sync - opcional (sync entre dispositivos)
#define FEATURE_NOTION   1   // Notion - tareas en Agenda
#define FEATURE_GITHUB   1   // GitHub - streak para desarrolladores
#define FEATURE_CALENDAR 1   // iCal + Agenda - tu calendario
#define FEATURE_QR_CODE  1   // QR para WiFi setup
#define FEATURE_LLM      1   // AI Agent - cloud LLM integration (Claude/OpenAI)

// ==================== SECRETS (from config_secrets.h, gitignored) ====================
// Create TaskyCircular/config_secrets.h with your API keys (see template in repo).
// If the file doesn't exist, everything compiles fine with empty defaults.
#if __has_include("config_secrets.h")
  #include "config_secrets.h"
#else
  #warning "config_secrets.h not found - all API keys will be empty. Copy the template to add your keys."
  #define SECRET_WIFI_SSID          ""
  #define SECRET_WIFI_PASS          ""
  #define SECRET_LLM_API_KEY        ""
  #define SECRET_LLM_PROVIDER       ""
  #define SECRET_WEATHER_CITY       ""
  #define SECRET_WEATHER_LAT        0
  #define SECRET_WEATHER_LON        0
  #define SECRET_NOTION_API_KEY     ""
  #define SECRET_NOTION_DATABASE_ID ""
  #define SECRET_GITHUB_USER        ""
  #define SECRET_GITHUB_TOKEN       ""
  #define SECRET_CALENDAR_URL       ""
  #define SECRET_FIREBASE_URL       ""
  #define SECRET_FIREBASE_SECRET    ""
  #define SECRET_SPOTIFY_CLIENT_ID  ""
  #define SECRET_SPOTIFY_SECRET     ""
#endif
