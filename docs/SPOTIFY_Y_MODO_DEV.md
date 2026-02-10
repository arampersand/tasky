# Tasky: Spotify, "Now Playing" y Modos usuario/dev

## Cómo funciona el "Now Playing" (Spotify API)

### Ruta actual: API de Spotify

1. **Credenciales**: Creas una app en [developer.spotify.com/dashboard](https://developer.spotify.com/dashboard).
2. **Client ID y Client Secret**: Se configuran en el portal WiFi de Tasky (solo rama **dev**).
3. **OAuth**: Al iniciar sesión, te redirige a Spotify, autorizas y vuelves a Tasky.
4. **Token**: Tasky guarda `access_token` y `refresh_token` y usa la API para:
   - `GET /me/player` → nombre de canción, artista, progreso, volumen
   - `PUT /me/player/play` → play
   - `PUT /me/player/pause` → pause
   - `POST /me/player/next` → siguiente
   - `PUT /me/player/volume` → volumen

### Requisitos

- Cuenta Spotify Premium
- App creada en el dashboard de Spotify
- Redirect URI: `http://<IP_TASKY>/spotify/callback`
- WiFi activo en Tasky

---

## Reproducción sin API key

### Opción A: BLE Media (recomendada para usuarios)

**Idea**: El móvil envía la info de "Now Playing" por Bluetooth al Tasky. No hace falta API key.

- **Android**: Media Control API expone lo que reproducen Spotify, YouTube Music, etc.
- **iOS**: Usa ANCS (Apple Notification Center Service) para notificaciones de media.

**Estado**: Solo existe el diseño en `ble_notifications.h` (BleMediaInfo). Falta implementar el servicio BLE y/o una app compañera.

**Para completar**:
1. Implementar `BleNotificationService` (BLE GATT server).
2. App móvil o uso de APIs nativas del sistema.
3. En la UI de Tasky, usar `BleMediaInfo` en lugar de `SpotifyPlayback` cuando no haya token de Spotify.

### Opción B: App compañera

Una app (React Native, Flutter, etc.) en el móvil:

1. Usuario inicia sesión en Spotify en la app.
2. La app obtiene tokens y hace las llamadas a la API.
3. La app envía por BLE al Tasky: `{track, artist, progress, playing}`.

El usuario nunca ve Client ID ni API keys.

---

## Modos: Tasky para usuarios vs para desarrolladores

| Aspecto | **Modo usuario** (main) | **Modo dev** (dev) |
|--------|-------------------------|---------------------|
| API keys | Ocultas | Visibles |
| Spotify | Mensaje BLE (proximamente) | OAuth completo |
| GitHub | Username visible | Username + guía token |
| Notion | Oculto | API key visible |
| Weather | Oculto | API key visible |
| Configuración | Simple | Técnica |

---

## Cambios para que funcione bien

### 1. Modo usuario (sin API keys)

- `TASKY_MODE_DEV 0` en `config_features.h` (rama main).
- Portal WiFi oculta Notion, Weather, Spotify config.
- Muestra mensaje: "Conecta tu telefono por Bluetooth para ver la musica".

### 2. Modo dev (con API keys)

- `TASKY_MODE_DEV 1` en `config_features.h` (rama dev).
- Portal WiFi muestra todas las opciones (Spotify OAuth, Notion, GitHub, Weather).
- Badge "Modo desarrollador" en el portal.

### 3. BLE Media (siguiente paso)

- Implementar `BleNotificationService` en ESP32.
- Usar `BleMediaInfo` como fuente de "Now Playing" cuando no haya Spotify configurado.
- En la pantalla de Spotify de la UI, mostrar BLE si está disponible, si no, Spotify API.

---

## Estructura de ramas

```
main   → Tasky para usuarios (TASKY_MODE_DEV 0) - sin API keys visibles
dev    → Tasky para desarrolladores (TASKY_MODE_DEV 1) - API keys, OAuth, etc.
```

**En `config_features.h`:**
- Rama `main`: `#define TASKY_MODE_DEV 0`
- Rama `dev`: `#define TASKY_MODE_DEV 1`
