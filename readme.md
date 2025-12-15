# Tasky ⏱️  
**Pomodoro & Task Tracker – ESP32 (Square TFT Edition)**

Tasky es un dispositivo físico de productividad basado en ESP32 que combina:
- Pomodoro timer
- Gestión simple de tareas
- Streak diario estilo GitHub
- Reloj independiente
- Configuración vía WiFi local (sin internet)
- Interfaz minimalista inspirada en productos “Nothing / Apple-like”

Esta versión está optimizada para **pantalla TFT cuadrada 240x240** y encoder rotativo.

---

## 🧠 Características

- ⏱ **Pomodoro**
  - Ajuste de tiempo con encoder (+5 minutos por giro)
  - Límite configurable hasta 90 minutos
  - Start / Pause con click
  - Al terminar: alarma visual + sonora

- ✅ **Tasks**
  - Hasta 3 tareas
  - Marcar como done con click
  - Cada tarea completada suma al streak diario

- 🔥 **Streak**
  - Cuadrícula 7x16 (112 días)
  - Estilo commits de GitHub
  - Navegación horizontal con encoder
  - Valores de 0 a 3 por día

- 🕒 **Clock**
  - Reloj HH:MM:SS
  - Editable desde el dispositivo
  - Frase minimalista: *EVERY SECOND COUNTS*

- 📡 **WiFi Local + QR**
  - El dispositivo crea su propia red WiFi
  - Portal web accesible desde el celular
  - Configuración sin internet
  - QR a pantalla completa para conexión rápida

- 🎨 **UI/UX**
  - Paleta oscura, minimalista
  - Tipografía limpia (FreeSans)
  - Animaciones suaves y sin ruido visual

---

## 🧩 Hardware usado

- **MCU:** Seeed Studio XIAO ESP32-C3  
- **Display:** TFT ST7789 240x240 (square)
- **Input:** Encoder rotativo con botón
- **Output:**
  - 2 LEDs (en serie con buzzer)
  - Buzzer
- **Otros:**
  - Botón adicional para cambiar de pantalla

---

## 🔌 Pinout (resumen)

| Función        | Pin |
|---------------|-----|
| TFT_BL        | D0 (2) |
| TFT_CS        | D1 (3) |
| TFT_DC        | D2 (4) |
| TFT_RST       | D3 (5) |
| TFT_SCK       | D8 (8) |
| TFT_MOSI     | D10 (10) |
| Encoder CLK  | D4 (6) |
| Encoder DT   | D5 (7) |
| Encoder SW   | D6 (21) |
| Mode Button  | D7 (20) |
| LED          | D9 (9) |
| Buzzer       | D1 (1) |

> ⚠️ Nota: LED y buzzer están físicamente en serie, por lo que no pueden activarse por separado vía software.

---

## 📦 Librerías necesarias (Arduino IDE)

Instalar desde **Library Manager**:

- Adafruit GFX Library
- Adafruit ST7789
- Preferences (ESP32 core)
- WiFi (ESP32 core)
- WebServer (ESP32 core)
- DNSServer (ESP32 core)

### QR Code
Se usa una librería compatible integrada manualmente  
(⚠️ no usar Nayuki directamente por conflicto con macros LOW/HIGH del ESP32).

---

## 🚀 Cómo flashear

1. Abrir `sketch_dec4a.ino` en Arduino IDE
2. Seleccionar:
   - Board: **XIAO ESP32C3**
   - Core ESP32: **3.3.3 (probado)**
3. Conectar el dispositivo por USB
4. Subir el sketch

---

## 📱 Uso rápido

- **Girar encoder:** ajustar valores / navegar
- **Click encoder:** seleccionar / confirmar
- **Botón lateral:** cambiar de pantalla
- **WiFi:**
  - Conectarse a la red `Tasky`
  - Password: `EverySecondCounts`
  - Abrir navegador → portal se abre solo (captive)

---

## 📁 Estructura del repo

