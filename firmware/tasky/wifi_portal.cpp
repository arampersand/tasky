#include "wifi_portal.h"
#include "config_features.h"
#include "network_manager.h"
#include "ota_service.h"
#include "version.h"

// HTML-escape a value reflected into the page (attributes and text).
static String esc(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += c;
    }
  }
  return out;
}

WiFiPortal::WiFiPortal(TaskyModel *m, TaskyNetManager *n, OtaService* otaSvc)
    : server(80), model(m), net(n), ota(otaSvc) {}

void WiFiPortal::begin() {
  if (wifiOn)
    return;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID.c_str(), model->cfg.apPass.c_str());

  IPAddress ip = WiFi.softAPIP();

  // Captive portal DNS: every name resolves to the AP so phones open setup
  dns.start(53, "*", ip);

  addRoutes();
  server.begin();
  wifiOn = true;
  touch();
}

void WiFiPortal::addRoutes() {
  // WebServer keeps handlers across stop()/begin(): register them once.
  if (routesAdded)
    return;
  routesAdded = true;

  static const char *headerKeys[] = {"Origin", "Referer"};
  server.collectHeaders(headerKeys, 2);

  server.on("/", HTTP_GET, [this]() { handleRoot(); });
  server.on("/save", HTTP_POST, [this]() { handleSave(); });

  // OTA update routes
  server.on("/ota/status", HTTP_GET, [this]() {
    if (authorized()) handleOtaStatus();
  });
  server.on("/ota/upload", HTTP_POST,
    [this]() {
      // Upload complete handler
      if (!authorized())
        return;
      if (ota && ota->getState() == OTA_SUCCESS) {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Update successful! Rebooting...\"}");
        delay(500);
        ota->rebootDevice();
      } else {
        String err = ota ? ota->getError() : "OTA not initialized";
        server.send(500, "application/json", "{\"success\":false,\"message\":\"" + err + "\"}");
      }
    },
    [this]() {
      // Upload handler (called for each chunk)
      handleOtaUpload();
    }
  );

  // Captive portal handlers
  auto redirect = [this]() {
    server.sendHeader(
        "Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  };

  server.on("/generate_204", HTTP_GET, redirect);
  server.on("/gen_204", HTTP_GET, redirect);
  server.on("/connecttest.txt", HTTP_GET, redirect);
  server.on("/redirect", HTTP_GET, redirect);
  server.on("/canonical.html", HTTP_GET, redirect);
  server.on("/hotspot-detect.html", HTTP_GET, [this]() { handleRoot(); });
  server.on("/ncsi.txt", HTTP_GET,
            [this]() { server.send(200, "text/plain", "Microsoft NCSI"); });
  server.onNotFound(redirect);
  server.on("/ping", HTTP_GET,
            [this]() { server.send(200, "text/plain", "pong"); });
}

void WiFiPortal::stop() {
  if (!wifiOn)
    return;
  dns.stop();
  server.stop();
  WiFi.softAPdisconnect(true);
  // Keep the home-network (station) connection alive for the services.
  WiFi.mode(model->cfg.wifiSSID.length() > 0 ? WIFI_STA : WIFI_OFF);
  wifiOn = false;
  Serial.println("[Portal] Setup network stopped");
}

void WiFiPortal::loop() {
  if (!wifiOn)
    return;
  dns.processNextRequest();
  server.handleClient();

  // Stay up while a phone is on the setup network or an update is armed/running.
  bool busy = WiFi.softAPgetStationNum() > 0 ||
              (ota && (ota->isArmed() || ota->getState() == OTA_WRITING));
  if (busy)
    touch();
  if (millis() - lastActivityAt > IDLE_STOP_MS)
    stop();
}

String WiFiPortal::getUptimeString() {
  uint32_t sec = millis() / 1000;
  uint32_t min = sec / 60;
  uint32_t hr = min / 60;
  sec %= 60;
  min %= 60;

  char buf[16];
  if (hr > 0) {
    snprintf(buf, sizeof(buf), "%luh %lum", hr, min);
  } else if (min > 0) {
    snprintf(buf, sizeof(buf), "%lum %lus", min, sec);
  } else {
    snprintf(buf, sizeof(buf), "%lus", sec);
  }
  return String(buf);
}

String WiFiPortal::htmlPage() {
  String s;
  s += "<!doctype html><html><head>";
  s += "<meta charset='UTF-8'>";
  s += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>Tasky - Setup</title>";
  s += "<style>";
  // Base styles
  s += "body{font-family:'SF Mono',Monaco,Consolas,monospace;background:#000;color:#fff;padding:16px;max-width:420px;margin:0 auto;font-size:13px}";
  s += "h2{text-transform:uppercase;letter-spacing:2px;border-bottom:1px solid #333;padding-bottom:8px;margin-bottom:8px}";
  s += "h3{color:#00e5cc;text-transform:uppercase;font-size:11px;letter-spacing:1px;margin:16px 0 8px 0}";
  s += "label{display:block;color:#888;font-size:10px;text-transform:uppercase;margin-top:10px;letter-spacing:1px}";
  s += "input,textarea,select{width:100%;padding:10px;margin:4px 0 6px 0;font-size:13px;background:#111;color:#fff;border:1px solid #333;box-sizing:border-box;font-family:inherit;border-radius:4px}";
  s += "input:focus,textarea:focus,select:focus{border-color:#00e5cc;outline:none}";
  s += "button{width:100%;padding:14px;margin:16px 0 8px 0;font-size:13px;background:#00e5cc;border:none;color:#000;font-weight:bold;text-transform:uppercase;letter-spacing:2px;cursor:pointer;border-radius:4px}";
  s += "button:hover{background:#00ffea}";
  s += "hr{border:none;border-top:1px solid #222;margin:16px 0}";
  s += "a{color:#00e5cc}";

  // Status bar styles
  s += ".status-bar{display:flex;justify-content:space-between;background:#111;padding:10px 12px;border-radius:6px;margin-bottom:16px;font-size:10px;flex-wrap:wrap;gap:8px}";
  s += ".status-bar .item{display:flex;align-items:center;gap:4px}";
  s += ".status-bar .label{color:#666}";
  s += ".status-bar .value{color:#fff}";
  s += ".status-bar .online{color:#0f0}";
  s += ".status-bar .offline{color:#f44}";

  // Welcome/intro section
  s += ".intro{background:#0a0a0a;padding:12px;border-radius:6px;margin-bottom:16px;border-left:3px solid #00e5cc}";
  s += ".intro h3{margin:0 0 8px 0;color:#00e5cc}";
  s += ".intro p{margin:0;color:#999;font-size:11px;line-height:1.5}";

  // Setup guide (collapsible)
  s += "details.guide{background:#0a0a0a;padding:0;border-radius:6px;margin:8px 0;border-left:3px solid #00e5cc}";
  s += "details.guide summary{cursor:pointer;color:#00e5cc;font-weight:bold;padding:12px;font-size:11px;text-transform:uppercase;letter-spacing:1px}";
  s += "details.guide summary:hover{background:#111}";
  s += "details.guide .content{padding:0 12px 12px 12px;font-size:11px;color:#ccc}";
  s += "details.guide ol{margin:8px 0;padding-left:20px}";
  s += "details.guide li{margin:6px 0;line-height:1.4}";
  s += "details.guide .link{display:inline-block;background:#1a1a1a;padding:4px 8px;border-radius:3px;margin:4px 0;font-size:10px;word-break:break-all}";

  // Task card
  s += ".task-card{background:#111;padding:10px;border-radius:4px;margin:6px 0}";
  s += ".task-status{display:flex;align-items:center;padding:4px 0;gap:8px}";
  s += ".task-done{color:#00e5cc}";
  s += ".task-pending{color:#666}";

  // Section card
  s += ".section{background:#0a0a0a;padding:12px;border-radius:6px;margin:12px 0}";

  s += "</style>";
  s += "</head><body>";

  // ===== HEADER =====
  s += "<h2>&#128337; Tasky</h2>";
#if TASKY_MODE_DEV
  s += "<div style='font-size:10px;color:#666;margin-bottom:12px;padding:6px;background:#1a1a1a;border-radius:4px'>&#128295; Modo desarrollador - integraciones avanzadas</div>";
#endif

  // ===== STATUS BAR =====
  s += "<div class='status-bar'>";
  s += "<div class='item'><span class='label'>WiFi:</span>";
  if (net && net->isConnected()) {
    s += "<span class='value online'>Conectado</span></div>";
  } else {
    s += "<span class='value offline'>Sin conexion</span></div>";
  }
  s += "<div class='item'><span class='label'>IP:</span><span class='value'>" + WiFi.softAPIP().toString() + "</span></div>";
  s += "<div class='item'><span class='label'>Uptime:</span><span class='value'>" + getUptimeString() + "</span></div>";
  s += "</div>";

  // ===== WELCOME INTRO =====
  s += "<div class='intro'>";
  s += "<h3>Bienvenido a Tasky</h3>";
  s += "<p>Tu asistente de productividad personal. Configura tu WiFi, integraciones y preferencias abajo.</p>";
  s += "</div>";

  // ===== SETUP GUIDES (Collapsible) =====
  s += "<h3>&#128218; Guias de Configuracion</h3>";

#if FEATURE_NOTION && TASKY_MODE_DEV
  // Notion Guide (solo modo dev)
  s += "<details class='guide'>";
  s += "<summary>&#128221; Como configurar Notion</summary>";
  s += "<div class='content'>";
  s += "<ol>";
  s += "<li>Ir a <a href='https://www.notion.so/my-integrations' target='_blank'>notion.so/my-integrations</a><div class='link'>notion.so/my-integrations</div></li>";
  s += "<li>Click <strong>\"New integration\"</strong> &rarr; Nombre: \"Tasky\"</li>";
  s += "<li>Copiar el <strong>\"Internal Integration Token\"</strong> (empieza con secret_...)</li>";
  s += "<li>En Notion, crear o abrir una base de datos de tareas</li>";
  s += "<li>Click <strong>\"Share\"</strong> &rarr; Invitar a \"Tasky\"</li>";
  s += "<li>Copiar el <strong>Database ID</strong> de la URL (32 caracteres despues de notion.so/)</li>";
  s += "</ol>";
  s += "<p style='color:#888;margin-top:8px'>Propiedades requeridas: Name (title), Status (checkbox o select), Due (date)</p>";
  s += "</div></details>";
#endif

#if FEATURE_CALENDAR
  // Google Calendar Guide
  s += "<details class='guide'>";
  s += "<summary>&#128197; Como obtener URL de Calendar</summary>";
  s += "<div class='content'>";
  s += "<ol>";
  s += "<li>Ir a <a href='https://calendar.google.com/calendar/r/settings' target='_blank'>Google Calendar Settings</a><div class='link'>calendar.google.com/calendar/r/settings</div></li>";
  s += "<li>En el menu izquierdo, click en tu calendario</li>";
  s += "<li>Scroll hasta <strong>\"Integrate calendar\"</strong></li>";
  s += "<li>Copiar <strong>\"Secret address in iCal format\"</strong></li>";
  s += "</ol>";
  s += "<p style='color:#888;margin-top:8px'>Esta URL sincroniza tus eventos de hoy en Tasky</p>";
  s += "</div></details>";
#endif

#if FEATURE_GITHUB && TASKY_MODE_DEV
  // GitHub Guide (solo modo dev)
  s += "<details class='guide'>";
  s += "<summary>&#128025; Configurar GitHub</summary>";
  s += "<div class='content'>";
  s += "<p>Solo necesitas tu <strong>nombre de usuario</strong> de GitHub para mostrar tu streak.</p>";
  s += "<p style='margin-top:8px'>Opcional: Para mayor rate limit, genera un <a href='https://github.com/settings/tokens' target='_blank'>Personal Access Token</a></p>";
  s += "<div class='link'>github.com/settings/tokens</div>";
  s += "</div></details>";
#endif

  // Focus Mode Guide
  s += "<details class='guide'>";
  s += "<summary>&#128737; Como usar Focus Mode</summary>";
  s += "<div class='content'>";
  s += "<p>Focus Mode se activa solo cuando corre una sesion de trabajo.</p>";
  s += "<ol>";
  s += "<li><strong>Luz:</strong> la barra de luz se enciende como senal de no molestar</li>";
  s += "<li><strong>Bloqueo:</strong> el boton MODO no cambia de pantalla; pausa el timer para salir</li>";
  s += "</ol>";
  s += "<p style='color:#888;margin-top:8px'>Se configura en el Tasky: MODO hasta More &rarr; Focus</p>";
  s += "</div></details>";

  // ===== FORM START =====
  s += "<form method='POST' action='/save'>";

  // ===== WIFI CONFIG =====
  s += "<hr><h3>&#128246; Conexion WiFi</h3>";
  s += "<label>SSID de tu WiFi</label>";
  s += "<input name='ssid' value='" + esc(model->cfg.wifiSSID) + "' maxlength='32' placeholder='Nombre de tu red WiFi'>";
  s += secretField("Password", "pass", model->cfg.wifiPass, "Contrasena de WiFi");
  s += "<label>Zona Horaria (UTC Offset)</label>";
  s += "<input name='tz' type='number' value='" + String(model->cfg.timezone) + "' min='-12' max='14' placeholder='-6 para CDMX'>";

  // ===== TASKS =====
  s += "<hr><h3>&#128203; Tareas de Hoy</h3>";
  for (int i = 0; i < 3; i++) {
    s += "<div class='task-card'>";
    s += "<label>Tarea " + String(i + 1) + "</label>";
    s += "<input name='t" + String(i) + "' value='" + esc(model->tasks[i]) + "' maxlength='40' placeholder='Nombre de la tarea'>";
    s += "<textarea name='td" + String(i) + "' maxlength='200' placeholder='Descripcion (opcional)' style='height:50px;resize:vertical'>" + esc(model->taskDescription[i]) + "</textarea>";
    String checkmark = model->taskDone[i] ? "&#10003; Completada" : "&#9744; Pendiente";
    String statusClass = model->taskDone[i] ? "task-done" : "task-pending";
    s += "<div class='task-status " + statusClass + "'>" + checkmark + "</div>";
    s += "</div>";
  }

  // ===== TIMER =====
  s += "<hr><h3>&#9201; Timer Pomodoro</h3>";
  s += "<label>Minutos por Sesion</label>";
  s += "<input name='work' type='number' value='" + String(model->cfg.workMin) + "' min='5' max='90'>";
  s += "<label>Inicio de jornada (hora, 0-23)</label>";
  s += "<input name='dsh' type='number' value='" + String(model->cfg.dayStartHour) + "' min='0' max='23'>";
  s += "<label>Fin de jornada (hora, 0-23)</label>";
  s += "<input name='deh' type='number' value='" + String(model->cfg.dayEndHour) + "' min='0' max='23'>";
  s += "<p style='font-size:11px;color:#888'>Tasky te da un resumen al empezar y al cerrar el dia.</p>";

  // ===== INTEGRATIONS =====
  s += "<hr><h3>&#128279; Integraciones</h3>";

#if FEATURE_GITHUB
  // GitHub
  s += "<div class='section'>";
  s += "<label>&#128025; GitHub Username</label>";
  s += "<input name='ghu' value='" + esc(model->cfg.githubUser) + "' placeholder='tu-username'>";
  s += "</div>";
#endif

#if FEATURE_CALENDAR
  // Calendar
  s += "<div class='section'>";
  s += secretField("&#128197; Calendar URL (iCal, secreta)", "cal", model->cfg.calendarUrl, "https://calendar.google.com/...");
  s += "</div>";
#endif

#if FEATURE_NOTION && TASKY_MODE_DEV
  // Notion (solo modo dev - requiere API key)
  s += "<div class='section'>";
  s += secretField("&#128221; Notion API Key", "nkey", model->cfg.notionApiKey, "secret_...");
  s += "<label>&#128221; Notion Database ID</label>";
  s += "<input name='ndb' value='" + esc(model->cfg.notionDatabaseId) + "' placeholder='abc123def456...'>";
  s += "</div>";
#endif

  // ===== STREAK =====
  s += "<hr><h3>&#128293; Streak</h3>";
  s += "<label>Fuente del Streak</label>";
  s += "<select name='sks'>";
  s += "<option value='0'" + String(model->cfg.streakSource == STREAK_GITHUB ? " selected" : "") + ">Solo GitHub</option>";
  s += "<option value='1'" + String(model->cfg.streakSource == STREAK_TASKY ? " selected" : "") + ">Solo Tasky</option>";
  s += "<option value='2'" + String(model->cfg.streakSource == STREAK_BOTH ? " selected" : "") + ">Combinado</option>";
  s += "</select>";

#if FEATURE_FIREBASE
  // ===== FIREBASE CLOUD SYNC =====
  s += "<hr><h3>&#9729; Cloud Sync (Firebase)</h3>";
  s += "<p style='font-size:10px;color:#666;margin-bottom:8px'>Sincroniza estadisticas a la nube para acceder desde cualquier lugar</p>";

  // Firebase Guide
  s += "<details class='guide'>";
  s += "<summary>&#128218; Como configurar Firebase</summary>";
  s += "<div class='content'>";
  s += "<ol>";
  s += "<li>Ir a <a href='https://console.firebase.google.com' target='_blank'>Firebase Console</a><div class='link'>console.firebase.google.com</div></li>";
  s += "<li>Click <strong>\"Create Project\"</strong> &rarr; Nombre: \"tasky-data\"</li>";
  s += "<li>Build &rarr; <strong>Realtime Database</strong> &rarr; Create</li>";
  s += "<li>Copiar la <strong>URL de la database</strong> (ej: tasky-xxx.firebaseio.com)</li>";
  s += "<li>Project Settings &rarr; Service Accounts &rarr; <strong>Database Secrets</strong></li>";
  s += "<li>Click \"Show\" y copiar el <strong>Secret</strong></li>";
  s += "</ol>";
  s += "<p style='color:#888;margin-top:8px'>Reglas de seguridad sugeridas: <code>{\"rules\": {\".read\": true, \".write\": true}}</code> (solo para uso personal)</p>";
  s += "</div></details>";

  s += "<div class='section'>";
  s += "<label>Firebase URL</label>";
  s += "<input name='fbUrl' value='" + esc(model->cfg.firebaseUrl) + "' placeholder='tu-proyecto.firebaseio.com'>";
  s += secretField("Firebase Secret", "fbKey", model->cfg.firebaseSecret, "Database secret...");
  s += "<label>Habilitar Sync</label>";
  s += "<select name='fbOn'>";
  s += model->cfg.firebaseEnabled ?
       "<option value='1' selected>ON - Sincronizar datos</option><option value='0'>OFF</option>" :
       "<option value='1'>ON - Sincronizar datos</option><option value='0' selected>OFF</option>";
  s += "</select>";
  s += "</div>";
#endif

#if FEATURE_WEATHER
  // ===== WEATHER WIDGET (Open-Meteo - no API key needed) =====
  s += "<hr><h3>&#9728; Weather Widget</h3>";
  s += "<p style='font-size:10px;color:#666;margin-bottom:8px'>Muestra el clima actual via Open-Meteo (gratis, sin API key)</p>";

  s += "<div class='section'>";
  s += "<label>Ciudad</label>";
  s += "<input name='wCity' value='" + esc(model->cfg.weatherCity) + "' placeholder='Chihuahua, Monterrey, etc.'>";
  s += "<label>Latitud (opcional - se autocompleta por ciudad)</label>";
  s += "<input name='wLat' type='number' step='0.0001' value='" + String(model->cfg.weatherLat, 4) + "' placeholder='28.6353'>";
  s += "<label>Longitud (opcional - se autocompleta por ciudad)</label>";
  s += "<input name='wLon' type='number' step='0.0001' value='" + String(model->cfg.weatherLon, 4) + "' placeholder='-106.0889'>";
  s += "<p style='font-size:10px;color:#666;margin-top:4px'>Deja lat/lon en 0 para geocodificacion automatica por ciudad</p>";
  s += "<label>Unidades de Temperatura</label>";
  s += "<select name='wCel'>";
  s += model->cfg.weatherCelsius ?
       "<option value='1' selected>Celsius</option><option value='0'>Fahrenheit</option>" :
       "<option value='1'>Celsius</option><option value='0' selected>Fahrenheit</option>";
  s += "</select>";
  s += "</div>";
#endif

#if FEATURE_SPOTIFY
  // ===== SPOTIFY / MUSIC =====
  s += "<hr><h3>&#127925; Now Playing</h3>";
#if TASKY_MODE_DEV
  s += "<p style='font-size:10px;color:#666;margin-bottom:8px'>Muestra y controla la musica via Spotify API (requiere Client ID/Secret)</p>";

  // Spotify Guide (solo modo dev)
  s += "<details class='guide'>";
  s += "<summary>&#128218; Como configurar Spotify</summary>";
  s += "<div class='content'>";
  s += "<ol>";
  s += "<li>Ir a <a href='https://developer.spotify.com/dashboard' target='_blank'>Spotify Developer Dashboard</a><div class='link'>developer.spotify.com/dashboard</div></li>";
  s += "<li>Click <strong>\"Create App\"</strong></li>";
  s += "<li>Nombre: \"Tasky\", Descripcion: \"Personal music display\"</li>";
  s += "<li>Redirect URI: <strong>http://" + WiFi.softAPIP().toString() + "/spotify/callback</strong></li>";
  s += "<li>En la app creada, copiar <strong>Client ID</strong> y <strong>Client Secret</strong></li>";
  s += "</ol>";
  s += "<p style='color:#888;margin-top:8px'>Nota: Requiere cuenta Spotify Premium para controles de reproduccion</p>";
  s += "</div></details>";

  s += "<div class='section'>";
  s += "<label>Spotify Client ID</label>";
  s += "<input name='spId' value='" + esc(model->cfg.spotifyClientId) + "' placeholder='abc123...'>";
  s += secretField("Spotify Client Secret", "spSec", model->cfg.spotifySecret, "xyz789...");

  // Show auth status
  if (model->cfg.spotifyToken.length() > 0) {
    s += "<div style='margin:8px 0;padding:10px;background:#143d1a;border-radius:4px;color:#0f0'>";
    s += "&#10003; Autenticado con Spotify";
    s += "</div>";
  } else if (model->cfg.spotifyClientId.length() > 0) {
    s += "<div style='margin:8px 0;padding:10px;background:#3d3d14;border-radius:4px;color:#ff0'>";
    s += "&#9888; Configurado pero sin autenticar";
    s += "</div>";
  }
  s += "</div>";
#else
  // Modo usuario: mensaje sin API keys
  s += "<p style='font-size:11px;color:#999;padding:12px;background:#0a0a0a;border-radius:6px;border-left:3px solid #00e5cc'>";
  s += "&#128276; Conecta tu telefono por Bluetooth para ver la musica que reproduces (Spotify, YouTube Music, etc.) sin configurar nada. ";
  s += "Usa la rama <strong>dev</strong> del firmware si quieres configurar Spotify con API keys.";
  s += "</p>";
#endif
#endif

#if FEATURE_LLM
  // ===== AI AGENT =====
  s += "<hr><h3>&#129302; AI Agent</h3>";
  s += "<p style='font-size:10px;color:#666;margin-bottom:8px'>Asistente de productividad con IA (Claude o GPT)</p>";

  // AI Guide
  s += "<details class='guide'>";
  s += "<summary>&#128218; Como configurar el AI Agent</summary>";
  s += "<div class='content'>";
  s += "<p><strong>Opcion A - Anthropic (Claude):</strong></p>";
  s += "<ol>";
  s += "<li>Ir a <a href='https://console.anthropic.com/settings/keys' target='_blank'>Anthropic Console</a><div class='link'>console.anthropic.com/settings/keys</div></li>";
  s += "<li>Click <strong>\"Create Key\"</strong></li>";
  s += "<li>Copiar la API key (empieza con sk-ant-...)</li>";
  s += "</ol>";
  s += "<p style='margin-top:8px'><strong>Opcion B - OpenAI (GPT):</strong></p>";
  s += "<ol>";
  s += "<li>Ir a <a href='https://platform.openai.com/api-keys' target='_blank'>OpenAI Platform</a><div class='link'>platform.openai.com/api-keys</div></li>";
  s += "<li>Click <strong>\"Create new secret key\"</strong></li>";
  s += "<li>Copiar la API key (empieza con sk-...)</li>";
  s += "</ol>";
  s += "<p style='color:#888;margin-top:8px'>Costo estimado: ~$0.01-0.03 por consulta con modelos economicos</p>";
  s += "</div></details>";

  s += "<div class='section'>";
  s += "<label>Proveedor de IA</label>";
  s += "<select name='lPrv'>";
  s += "<option value='0'" + String(model->cfg.llmProvider == 0 ? " selected" : "") + ">Anthropic (Claude)</option>";
  s += "<option value='1'" + String(model->cfg.llmProvider == 1 ? " selected" : "") + ">OpenAI (GPT)</option>";
  s += "</select>";
  s += secretField("API Key", "lKey", model->cfg.llmApiKey, "sk-ant-... o sk-...");
  s += "</div>";
#endif

  // ===== SUBMIT =====
  s += "<button type='submit'>&#128190; Guardar y Conectar</button>";
  s += "</form>";

  // ===== OTA UPDATE SECTION (Outside form) =====
  s += "<hr><h3>&#128229; Firmware Update</h3>";
  s += "<p style='font-size:10px;color:#666;margin-bottom:8px'>Actualiza el firmware via WiFi</p>";
  s += "<p style='font-size:12px;color:#ff6b35'>Primero en el Tasky: MODO hasta More &rarr; Update y presiona la perilla. La actualizacion queda habilitada 5 minutos.</p>";

  // Current version
  String version = ota ? ota->getVersion() : "Unknown";
  s += "<div style='margin:8px 0;padding:10px;background:#111;border-radius:4px'>";
  s += "<span style='color:#888'>Version actual:</span> <span style='color:#00e5cc;font-weight:bold'>v" + version + "</span>";
  s += "</div>";

  // Upload form
  s += "<form id='otaForm' enctype='multipart/form-data' style='margin-top:12px'>";
  s += "<label>Archivo de Firmware (.bin)</label>";
  s += "<input type='file' id='firmware' name='firmware' accept='.bin' style='padding:8px'>";
  s += "<button type='button' id='uploadBtn' onclick='uploadFirmware()' style='background:#ff6b35'>&#128640; Actualizar Firmware</button>";
  s += "</form>";

  // Progress bar
  s += "<div id='otaProgress' style='display:none;margin-top:12px'>";
  s += "<div style='background:#222;border-radius:4px;overflow:hidden;height:24px'>";
  s += "<div id='progressBar' style='background:#00e5cc;height:100%;width:0%;transition:width 0.3s'></div>";
  s += "</div>";
  s += "<p id='progressText' style='text-align:center;margin:8px 0;color:#888'>0%</p>";
  s += "</div>";

  // Status message
  s += "<div id='otaStatus' style='display:none;margin-top:12px;padding:12px;border-radius:4px'></div>";

  // JavaScript for OTA upload
  s += "<script>";
  s += "function uploadFirmware() {";
  s += "  var fileInput = document.getElementById('firmware');";
  s += "  var file = fileInput.files[0];";
  s += "  if (!file) { alert('Selecciona un archivo .bin'); return; }";
  s += "  if (!file.name.endsWith('.bin')) { alert('Solo archivos .bin'); return; }";
  s += "  ";
  s += "  var formData = new FormData();";
  s += "  formData.append('firmware', file);";
  s += "  ";
  s += "  document.getElementById('uploadBtn').disabled = true;";
  s += "  document.getElementById('uploadBtn').textContent = 'Subiendo...';";
  s += "  document.getElementById('otaProgress').style.display = 'block';";
  s += "  document.getElementById('otaStatus').style.display = 'none';";
  s += "  ";
  s += "  var xhr = new XMLHttpRequest();";
  s += "  xhr.open('POST', '/ota/upload', true);";
  s += "  ";
  s += "  xhr.upload.onprogress = function(e) {";
  s += "    if (e.lengthComputable) {";
  s += "      var pct = Math.round((e.loaded / e.total) * 100);";
  s += "      document.getElementById('progressBar').style.width = pct + '%';";
  s += "      document.getElementById('progressText').textContent = pct + '%';";
  s += "    }";
  s += "  };";
  s += "  ";
  s += "  xhr.onload = function() {";
  s += "    var status = document.getElementById('otaStatus');";
  s += "    status.style.display = 'block';";
  s += "    try {";
  s += "      var resp = JSON.parse(xhr.responseText);";
  s += "      if (resp.success) {";
  s += "        status.style.background = '#143d1a';";
  s += "        status.style.color = '#0f0';";
  s += "        status.innerHTML = '&#10003; ' + resp.message;";
  s += "        document.getElementById('uploadBtn').textContent = 'Reiniciando...';";
  s += "      } else {";
  s += "        status.style.background = '#3d1414';";
  s += "        status.style.color = '#f44';";
  s += "        status.innerHTML = '&#10007; Error: ' + resp.message;";
  s += "        document.getElementById('uploadBtn').disabled = false;";
  s += "        document.getElementById('uploadBtn').textContent = 'Actualizar Firmware';";
  s += "      }";
  s += "    } catch(e) {";
  s += "      status.style.background = '#3d1414';";
  s += "      status.style.color = '#f44';";
  s += "      status.innerHTML = '&#10007; Error de conexion';";
  s += "      document.getElementById('uploadBtn').disabled = false;";
  s += "      document.getElementById('uploadBtn').textContent = 'Actualizar Firmware';";
  s += "    }";
  s += "  };";
  s += "  ";
  s += "  xhr.onerror = function() {";
  s += "    var status = document.getElementById('otaStatus');";
  s += "    status.style.display = 'block';";
  s += "    status.style.background = '#3d1414';";
  s += "    status.style.color = '#f44';";
  s += "    status.innerHTML = '&#10007; Error de conexion';";
  s += "    document.getElementById('uploadBtn').disabled = false;";
  s += "    document.getElementById('uploadBtn').textContent = 'Actualizar Firmware';";
  s += "  };";
  s += "  ";
  s += "  xhr.send(formData);";
  s += "}";
  s += "</script>";

  // ===== FOOTER =====
  s += "<div style='text-align:center;margin:20px 0;padding:16px;border-top:1px solid #222;font-size:10px;color:#666'>";
  s += "<p>Tasky v" TASKY_VERSION " &bull; <a href='https://github.com/arampersand/tasky' target='_blank'>GitHub</a></p>";
  s += "</div>";

  s += "</body></html>";
  return s;
}

String WiFiPortal::secretField(const char *label, const char *name, const String &stored,
                               const char *placeholder, const char *type) {
  String s = "<label>" + String(label) + "</label>";
  bool isSet = stored.length() > 0;
  s += "<input name='" + String(name) + "' type='" + String(type) + "' value='' autocomplete='off' placeholder='" +
       (isSet ? String("Guardada - deja vacio para conservarla") : esc(placeholder)) + "'>";
  if (isSet) {
    s += "<label style='display:flex;gap:6px;align-items:center;text-transform:none;color:#0c8'>&#10003; Configurada"
         "<span style='margin-left:auto;color:#888'><input type='checkbox' name='" + String(name) +
         "_clr' value='1' style='width:auto;margin:0'> borrar</span></label>";
  }
  return s;
}

void WiFiPortal::applySecret(const char *name, String &stored) {
  if (server.hasArg(String(name) + "_clr")) {
    stored = "";
    return;
  }
  if (server.hasArg(name) && server.arg(name).length() > 0)
    stored = server.arg(name);  // empty submit keeps the stored value
}

// Blocks CSRF (another site POSTing to Tasky with the browser's cached login)
// and DNS rebinding: on the LAN the Host must be Tasky's own IP, and an
// Origin/Referer, when present, must point at that same host.
bool WiFiPortal::sameOrigin() {
  String host = server.hostHeader();
  int colon = host.indexOf(':');
  if (colon >= 0)
    host = host.substring(0, colon);
  if (host != WiFi.localIP().toString())
    return false;
  for (const char *h : {"Origin", "Referer"}) {
    String v = server.header(h);
    if (v.length() == 0)
      continue;
    int start = v.indexOf("://");
    start = start < 0 ? 0 : start + 3;
    int end = start;
    while (end < (int)v.length() && v[end] != '/' && v[end] != ':')
      end++;
    if (v.substring(start, end) != host)
      return false;
  }
  return true;
}

bool WiFiPortal::authorized() {
  if (server.client().localIP() == WiFi.softAPIP())
    return true;  // connected through the setup AP
  if (!sameOrigin()) {
    server.send(403, "text/plain", "Forbidden");
    return false;
  }
  if (server.authenticate(LAN_USER, model->cfg.apPass.c_str()))
    return true;
  server.requestAuthentication(BASIC_AUTH, "Tasky",
                               "Login: tasky + the password shown on Tasky (More > Settings > WiFi)");
  return false;
}

void WiFiPortal::handleRoot() {
  if (!authorized())
    return;
  touch();
  server.send(200, "text/html", htmlPage());
}

void WiFiPortal::handleOtaStatus() {
  if (!ota) {
    server.send(500, "application/json", "{\"error\":\"OTA not initialized\"}");
    return;
  }

  String json = "{";
  json += "\"state\":" + String(ota->getState()) + ",";
  json += "\"progress\":" + String(ota->getProgress()) + ",";
  json += "\"version\":\"" + ota->getVersion() + "\",";
  json += "\"error\":\"" + ota->getError() + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

void WiFiPortal::handleOtaUpload() {
  if (!ota) return;

  HTTPUpload& upload = server.upload();

  touch();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[OTA] Upload started: %s\n", upload.filename.c_str());

    // Unauthenticated LAN uploads are dropped (the final handler replies 401)
    if (server.client().localIP() != WiFi.softAPIP() &&
        (!sameOrigin() || !server.authenticate(LAN_USER, model->cfg.apPass.c_str()))) {
      ota->abortUpdate();
      return;
    }

    // Validate file extension
    if (!upload.filename.endsWith(".bin")) {
      ota->abortUpdate();
      return;
    }

    // upload.totalSize is still 0 at FILE_START; the request's Content-Length
    // (file + multipart overhead) is only used to estimate progress.
    if (!ota->startUpdate(server.clientContentLength())) {
      Serial.println("[OTA] Failed to start update");
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    // Write chunk
    if (ota->getState() == OTA_WRITING) {
      if (!ota->writeChunk(upload.buf, upload.currentSize)) {
        Serial.println("[OTA] Failed to write chunk");
      }
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    Serial.printf("[OTA] Upload complete: %d bytes\n", upload.totalSize);
    ota->finishUpdate();
  }
  else if (upload.status == UPLOAD_FILE_ABORTED) {
    Serial.println("[OTA] Upload aborted");
    ota->abortUpdate();
  }
}

void WiFiPortal::handleSave() {
  if (!authorized())
    return;
  touch();
  for (int i = 0; i < 3; i++) {
    String k = "t" + String(i);
    if (server.hasArg(k)) {
      String name = server.arg(k);
      if (name.length() == 0)
        name = "Empty";
      if (name != model->tasks[i]) {
        // New task in this slot: starts undone with zero focus time
        model->taskFocusMin[i] = 0;
        model->taskDone[i] = false;
        if (model->currentTask == i)
          model->currentTask = -1;
      }
      model->tasks[i] = name;
    }

    // Task descriptions
    String kd = "td" + String(i);
    if (server.hasArg(kd)) {
      model->taskDescription[i] = server.arg(kd);
    }
  }

  if (server.hasArg("work")) {
    int v = server.arg("work").toInt();
    if (v < 5)
      v = 5;
    if (v > 90)
      v = 90;
    model->cfg.workMin = (uint8_t)v;
  }
  if (server.hasArg("dsh"))
    model->cfg.dayStartHour = (uint8_t)constrain(server.arg("dsh").toInt(), 0, 23);
  if (server.hasArg("deh"))
    model->cfg.dayEndHour = (uint8_t)constrain(server.arg("deh").toInt(), 0, 23);

  if (server.hasArg("ssid"))
    model->cfg.wifiSSID = server.arg("ssid");
  applySecret("pass", model->cfg.wifiPass);
  if (server.hasArg("tz"))
    model->cfg.timezone = (int8_t)server.arg("tz").toInt();

#if FEATURE_GITHUB
  if (server.hasArg("ghu"))
    model->cfg.githubUser = server.arg("ghu");
#endif
#if FEATURE_CALENDAR
  applySecret("cal", model->cfg.calendarUrl);
#endif

  // Notion Integration
#if FEATURE_NOTION
  applySecret("nkey", model->cfg.notionApiKey);
  if (server.hasArg("ndb"))
    model->cfg.notionDatabaseId = server.arg("ndb");
#endif

  // Streak Source
  if (server.hasArg("sks")) {
    int v = server.arg("sks").toInt();
    if (v >= 0 && v <= 2) {
      model->cfg.streakSource = (StreakSource)v;
    }
  }

#if FEATURE_FIREBASE
  // Firebase Cloud Sync
  if (server.hasArg("fbUrl"))
    model->cfg.firebaseUrl = server.arg("fbUrl");
  applySecret("fbKey", model->cfg.firebaseSecret);
  if (server.hasArg("fbOn"))
    model->cfg.firebaseEnabled = server.arg("fbOn") == "1";
#endif

#if FEATURE_WEATHER
  // Weather Widget (Open-Meteo)
  {
    String oldCity = model->cfg.weatherCity;
    if (server.hasArg("wCity"))
      model->cfg.weatherCity = server.arg("wCity");
    if (server.hasArg("wLat"))
      model->cfg.weatherLat = server.arg("wLat").toFloat();
    if (server.hasArg("wLon"))
      model->cfg.weatherLon = server.arg("wLon").toFloat();
    if (server.hasArg("wCel"))
      model->cfg.weatherCelsius = server.arg("wCel") == "1";
    // Reset lat/lon if city changed (force re-geocode)
    if (oldCity != model->cfg.weatherCity && model->cfg.weatherCity.length() > 0) {
      model->cfg.weatherLat = 0;
      model->cfg.weatherLon = 0;
    }
  }
#endif

#if FEATURE_SPOTIFY
  // Spotify Integration
  if (server.hasArg("spId"))
    model->cfg.spotifyClientId = server.arg("spId");
  applySecret("spSec", model->cfg.spotifySecret);
#endif

#if FEATURE_LLM
  // LLM Agent
  applySecret("lKey", model->cfg.llmApiKey);
  if (server.hasArg("lPrv")) {
    int v = server.arg("lPrv").toInt();
    if (v >= 0 && v <= 1)
      model->cfg.llmProvider = (uint8_t)v;
  }
#endif

  model->save();

  // [NEW] Trigger reconnection
  if (net) {
    net->disconnect(); // Clear old state
    net->begin();      // Start new connection attempt
  }

  server.send(200, "text/html",
              "<h3>Saved & Verify...</h3><p>Check device screen for connection "
              "status.</p><p><a href='/'>Back</a></p>");
}
