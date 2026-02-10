#pragma once

// ==================== FEATURE FLAGS (for firmware size reduction) ====================
// Set to 1 to enable, 0 to disable. Disabled features are excluded from compilation.
// Config: Huge APP (3MB) - features esenciales para asistente de productividad.

#define FEATURE_WEATHER  1   // Clima - contexto del dia
#define FEATURE_SPOTIFY  1   // Spotify - musica durante sesiones
#define FEATURE_FIREBASE 0   // Cloud sync - opcional (sync entre dispositivos)
#define FEATURE_NOTION   1   // Notion - tareas en Agenda
#define FEATURE_GITHUB   1   // GitHub - streak para desarrolladores
#define FEATURE_CALENDAR 1   // iCal + Agenda - tu calendario
#define FEATURE_QR_CODE  1   // QR para WiFi setup
