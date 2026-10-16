<div align="center">

<img src="docs/images/tasky-focus.jpg" alt="Tasky en una sesión de foco de 25 minutos" width="420">

# Tasky

**Un pequeño compañero de escritorio open source que te mantiene enfocado.**<br>
Una perilla, una pantalla y una barra de luz: temporizador de foco, tus tareas del día, tu racha, tu próxima reunión y un agente de IA, sin agarrar el celular.

[![ci](https://github.com/arampersand/tasky/actions/workflows/ci.yml/badge.svg)](https://github.com/arampersand/tasky/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/arampersand/tasky?color=29cebd&label=firmware)](https://github.com/arampersand/tasky/releases/latest)
[![license](https://img.shields.io/badge/license-GPL--3.0-3f7dff)](LICENSE)

### [⚡ Flashéalo desde el navegador →](https://tasky-flash.vercel.app)

[Manual](docs/MANUAL.md) · [Hardware](docs/HARDWARE.md) · [Compilar](docs/BUILDING.md) · [Haz el tuyo](docs/CUSTOMIZING.md) · [Arquitectura](docs/ARCHITECTURE.md) · [Roadmap](docs/ROADMAP.md) · [English](README.md)

<sub>La documentación detallada está en inglés.</sub>

</div>

---

## Por qué Tasky

Tu celular es la peor herramienta de foco que tienes. Tasky es un objeto de un solo propósito que vive junto a tu teclado. Giras la perilla, haces click y ya estás trabajando. La barra de luz le avisa a quien está cerca que estás concentrado. Todo es abierto: el firmware, la documentación y el flasher web, así que puedes armar uno con piezas comunes y hacerlo tuyo.

## Qué hace

| | |
|---|---|
| ⏱ **Temporizador de foco** | Sesiones tipo Pomodoro con anillo de progreso, puntos de sesión y alarma. El modo foco enciende la barra de luz y bloquea el botón MODE. |
| ✅ **Tareas** | Tres tareas para hoy, con minutos de foco por tarea. Completarlas alimenta tu racha. |
| 🔥 **Racha** | Cuadrícula estilo GitHub, alimentada por tus sesiones y, si quieres, tus contribuciones reales de GitHub. |
| 📅 **Calendario y agenda** | Cualquier feed iCal. Aviso 5 minutos antes de cada reunión, resumen de la mañana y cierre del día. |
| 🌤 **Clima** | Open-Meteo, sin API key. |
| 🤖 **Agente IA** | Con tu key de Claude u OpenAI. |
| 📶 **Portal WiFi** | Tasky crea su propia red con portal cautivo y QR. Se configura todo desde el celular. |
| 🔄 **Actualizaciones** | Desde el navegador, o por WiFi (OTA armado desde el propio dispositivo). |

## Flashear en 60 segundos

1. Abre **[tasky-flash.vercel.app](https://tasky-flash.vercel.app)** en Chrome o Edge desde una computadora.
2. Elige **XIAO ESP32-C3** o **C6** y conecta la placa con un cable USB-C *de datos*. Si no aparece el puerto, mantén `BOOT` presionado al conectarla.
3. Click en **Connect & flash**.
4. En Tasky, doble click en **MODE** → **More** → **Setup (QR)**. Escanea el QR (o conéctate a la red **`Tasky`** con la contraseña que aparece en pantalla; cada Tasky tiene la suya) y se abre la página de configuración (o entra a `192.168.4.1`).

La página siempre sirve la [release](https://github.com/arampersand/tasky/releases/latest) más nueva y conserva tus ajustes al reinstalar.

## Arma uno

Piezas: XIAO ESP32-C3 o C6, pantalla ST7789 de 1.54" 240×240 (o redonda GC9A01A), encoder rotativo con botón, botón táctil, buzzer activo con LED(s) en serie y una carcasa impresa en 3D. Pinout y cableado en **[docs/HARDWARE.md](docs/HARDWARE.md)**.

```bash
git clone https://github.com/arampersand/tasky && cd tasky/firmware
pio run -e xiao_c3 -t upload && pio device monitor
```

## Haz tu propia versión

**[docs/CUSTOMIZING.md](docs/CUSTOMIZING.md)** explica cómo activar y desactivar features, cambiar a pantalla redonda, remapear pines, cambiar colores y textos, **agregar tu propia pantalla o integración** y **publicar tu fork con su propio flasher web** (tag → release → Vercel).

## Lo que viene

**Tasky 3.0**: configuración completa desde el navegador (WiFi → calendario → GitHub → IA), una pantalla principal para ver todo de un vistazo con frases, y dos variantes: **Tasky Desk** (compañero de tu compu: volumen, agentes de Claude Code) y **Tasky Go** (con batería y notificaciones del celular). Detalles en **[docs/ROADMAP.md](docs/ROADMAP.md)**.

## Contribuir

Los PRs son bienvenidos. Lee **[CONTRIBUTING.md](CONTRIBUTING.md)**. El CI compila ambas placas en cada PR, y **[docs/IMPROVEMENTS.md](docs/IMPROVEMENTS.md)** tiene la lista priorizada de mejoras, ideal para empezar.

## Licencia

[GPL-3.0](LICENSE). Puedes usar, modificar y vender Tasky, pero el firmware derivado debe seguir siendo abierto con la misma licencia.

Hecho por [@arampersand](https://github.com/arampersand).
