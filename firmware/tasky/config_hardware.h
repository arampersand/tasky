#pragma once

// ==================== DEBUG / BRING-UP ====================
// Set to 1 to run display diagnostics (Ultimate Scanner)
// Set to 0 for normal Tasky operation
#define DISPLAY_BRINGUP 0
// Set to 0 to use Hardware SPI (Recommended)
#define USE_SW_SPI 0

// ==================== PORTABLE PIN DEFINITIONS ====================
// Uses D-pin constants (D0-D10) to support both XIAO ESP32C3 and C6
// automagically if compiled with the correct board selected.

// SPI DISPLAY (Right Side)
#define TFT_CS D1    // Chip Select
#define TFT_DC D2    // Data/Command
#define TFT_RST D3   // Reset
#define TFT_SCK D8   // SPI Clock (SCL)
#define TFT_MOSI D10 // SPI MOSI (SDA)
#define TFT_MISO D9  // Not used (But reserved)

// INPUTS (Left Side)
#define ENCODER_CLK D4 // Was GPIO 6 on C3
#define ENCODER_DT D5  // Was GPIO 7 on C3
#define ENCODER_SW D6  // Was GPIO 21 on C3
#define MODO_BTN D7    // Was GPIO 20 on C3

// PERIPHERALS
#define LED_PIN                                                                \
  D9 // Neopixels? Careful with D9/MISO conflict if used.
     // Note: User schematic shows LEDs on D9.
     // Standard XIAO MISO is D9.
     // If LEDs are here, SPI MISO cannot be used (OK for display).

// Light bar type is a runtime setting (Config::ledType, web setup "Light"):
//   0 = one or two plain LEDs, PWM (Tasky v1.1 board, hand-wired builds)
//   1 = chain of SK6812MINI RGB LEDs on LED_PIN (Tasky v3.1 board)
#define RGB_LED_COUNT 6
#define RGB_LED_COLOR 0xFF6000 // amber; scaled by Settings -> LED

#define ACTIVE_BUZZER 1 // 1 = Active Buzzer (High/Low), 0 = Passive (PWM)
#define BUZZER_PIN D0   // Was GPIO 2 on C3
#define TFT_BL D0       // Backlight (D0 segun doc) - si compartido con Buzzer, mantener HIGH al terminar

// ==================== DISPLAY CONFIG ====================
#define SCREEN_WIDTH 240
// 240 = pantalla cuadrada 240x240 | 280 = rectangular 240x280
#define SCREEN_HEIGHT 240
// Offset ST7789 240x240 (1.54" IPS) - 0 para paneles que muestran filas 0-239
// Si ves franja de ruido arriba, prueba 40 o 80
#define ROW_OFFSET 0
#define COL_OFFSET 0
