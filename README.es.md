<div align="center">

<img src="docs/images/tasky-hero.jpg" alt="Tasky en una sesión de foco de 25 minutos" width="640">

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

## Un firmware con todo

Desde la **3.0** hay un solo firmware para las dos placas (XIAO C3 y C6) con todo: Home de un vistazo, Pomodoro, tareas, calendario, clima, racha de GitHub, Notion, pregúntale a ChatGPT/Claude, **tarjetas de agentes de Claude Code** (la luz respira cuando un agente te necesita y apruebas o rechazas permisos con la perilla) y una **fila de teclas Bluetooth** (volumen, brillo, música y atajos para Mac o Windows). Agents y Controls están en el carrusel desde el inicio; el Bluetooth solo arranca la primera vez que abres Controls.

**Conecta Claude Code**: dentro de Claude Code, corre

```
/plugin marketplace add arampersand/tasky
/plugin install tasky@tasky
```

y reinicia Claude Code; Tasky muestra una tarjeta *Ready*. ¿Algo no funciona? Corre `/tasky:status`. ¿Sin plugin? [El instalador por terminal](companion/README.md#without-the-plugin-terminal) hace lo mismo.

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
| 🧑‍💻 **Agentes de Claude Code** | Una tarjeta por sesión; la luz respira cuando un agente te espera; apruebas o rechazas permisos con la perilla. |
| 🎛 **Controls** | Tasky como teclado Bluetooth: volumen, brillo, música y atajos para Mac o Windows. |
| 🏠 **Home de un vistazo** | Hora, clima, lo que sigue, tus pendientes y una frase que va cambiando. Presiona la perilla para enfocarte. |
| 🪄 **Setup en dos minutos** | Justo después de flashear, la página te guía por WiFi, calendario, tareas, GitHub e IA por USB. O escanea un QR con el celular. |
| 🔄 **Actualizaciones** | Desde el navegador, o por WiFi (OTA armado desde el propio dispositivo). |

## Flashear en 60 segundos

1. Abre **[tasky-flash.vercel.app](https://tasky-flash.vercel.app)** en Chrome o Edge desde una computadora.
2. Elige **XIAO ESP32-C3** o **C6** y conecta la placa con un cable USB-C *de datos*. Si no aparece el puerto, mantén `BOOT` presionado al conectarla.
3. Click en **Connect & flash**.
4. Déjalo conectado y sigue **Set up** en la misma página: WiFi → calendario → tareas → GitHub → IA → clima, todo por el cable USB. (¿Sin compu? En Tasky abre **Settings → More → Setup (QR)** y escanea el código con el celular.)

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

**Tasky 3.0** junta los dos firmwares de la 2.x (Tasky y Tasky for Agents) en uno. Lo siguiente: **Tasky Go** (con batería y notificaciones del celular), más agentes (Codex, Cursor). Detalles en **[docs/ROADMAP.md](docs/ROADMAP.md)**.

## Contribuir

Los PRs son bienvenidos. Lee **[CONTRIBUTING.md](CONTRIBUTING.md)**. El CI compila ambas placas en cada PR, y **[docs/IMPROVEMENTS.md](docs/IMPROVEMENTS.md)** tiene la lista priorizada de mejoras, ideal para empezar.

## Licencia

[GPL-3.0](LICENSE). Puedes usar, modificar y vender Tasky, pero el firmware derivado debe seguir siendo abierto con la misma licencia.

Hecho por [@arampersand](https://github.com/arampersand).
