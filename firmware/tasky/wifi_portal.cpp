#include "wifi_portal.h"
#include "config_features.h"
#include "network_manager.h"
#include "ota_service.h"
#include "version.h"

// Same limits as the USB setup protocol (usb_setup.cpp)
static const int MAX_PHRASES = 12;
static const unsigned MAX_PHRASE_LEN = 60;

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

// The screen font is plain ASCII: "Mañana sí" becomes "Manana si". Latin-1
// letters lose their accents, other non-ASCII and control bytes are dropped,
// runs of spaces collapse (mirrors the web setup's ascii()).
static String asciiFold(const String &in) {
  static const char LATIN1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = in[i];
    if (c >= 0x20 && c < 0x7f) {
      if (c != ' ' || !out.endsWith(" "))
        out += (char)c;
    } else if (c == 0xC3 && i + 1 < in.length() && (uint8_t)in[i + 1] >= 0x80 &&
               (uint8_t)in[i + 1] <= 0xBF) {
      out += LATIN1[(uint8_t)in[++i] - 0x80];
    }
  }
  out.trim();
  return out;
}

static const char CSS[] =
    "*{box-sizing:border-box}"
    "body{margin:0;background:#0b0e0f;color:#e9eeee;font:16px/1.45 -apple-system,system-ui,'Segoe UI',Roboto,sans-serif}"
    "main{max-width:480px;margin:0 auto;padding:12px 16px 24px}"
    "header{display:flex;justify-content:space-between;align-items:center}"
    "h1{font-size:24px;margin:8px 0}"
    "h2{font-size:18px;margin:0 0 4px;display:flex;align-items:center;gap:10px}"
    "h2 b{display:inline-grid;place-items:center;width:26px;height:26px;border-radius:50%;background:#29cbbd;color:#06201d;font-size:14px}"
    "section,.mo{background:#151a1b;border-radius:14px;padding:16px;margin:12px 0}"
    ".h{color:#98a4a6;font-size:14px;margin:4px 0 8px}"
    "label{display:block;font-size:14px;color:#c4cdce;margin:12px 0 0}"
    "input,select,textarea{display:block;width:100%;margin-top:4px;padding:12px;font:inherit;font-size:16px;color:#fff;background:#0b0e0f;border:1px solid #2c3537;border-radius:10px}"
    "input:focus,select:focus,textarea:focus{outline:none;border-color:#29cbbd}"
    "input[type=checkbox]{display:inline;width:auto;margin:0 4px 0 0}"
    "summary{cursor:pointer;font-weight:600}"
    ".c{border:1px solid #2c3537;border-radius:10px;padding:10px 12px;margin:6px 0}"
    ".c ol{margin:8px 0 2px;padding-left:20px;font-size:14px}"
    ".mo summary{font-size:18px}"
    ".sv{display:flex;justify-content:space-between;font-size:14px;margin-top:4px}"
    ".ok{color:#29cbbd}.er{color:#ff6b6b}"
    "a{color:#29cbbd}code{word-break:break-all}"
    ".sb{position:sticky;bottom:0;padding:12px 0;background:linear-gradient(transparent,#0b0e0f 30%)}"
    "button{width:100%;padding:14px;font:inherit;font-weight:700;color:#06201d;background:#29cbbd;border:0;border-radius:12px}"
    "progress{width:100%;margin-top:8px}";

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

  // Background scan: its results fill the page's network suggestions.
  // Non-blocking; the request handlers only read what it found.
  WiFi.scanNetworks(true);

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

  static const char *headerKeys[] = {"Origin", "Referer", "Accept-Language"};
  server.collectHeaders(headerKeys, 3);

  server.on("/", HTTP_GET, [this]() { handleRoot(); });
  server.on("/save", HTTP_POST, [this]() { handleSave(); });
  // The phone's clock: lets Tasky show the time before (or without) internet
  server.on("/time", HTTP_POST, [this]() {
    if (!authorized())
      return;
    if (server.hasArg("tz"))
      model->cfg.timezone = (int8_t)constrain(server.arg("tz").toInt(), -12, 14);
    if (server.hasArg("t"))
      model->setClock((time_t)atoll(server.arg("t").c_str()));
    server.send(204);
  });

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

void WiFiPortal::pickLang() {
  String lang = server.arg("lang");  // ?lang=, or the form's hidden field
  if (lang == "es" || lang == "en") {
    es = lang == "es";
    return;
  }
  String al = server.header("Accept-Language");
  al.toLowerCase();
  es = al.startsWith("es") || al.indexOf("es") >= 0;
}

void WiFiPortal::pageHead(String &s) {
  s += "<!doctype html><html lang='";
  s += T("en", "es");
  s += "'><head><meta charset='UTF-8'>"
       "<meta name='viewport' content='width=device-width,initial-scale=1'>"
       "<meta name='color-scheme' content='dark'><title>Tasky</title><style>";
  s += CSS;
  s += "</style></head><body><main>";
}

String WiFiPortal::htmlPage() {
  String s;
  s.reserve(11000);
  pageHead(s);
  int n = 0;  // section number
  auto section = [&](const char *title) {
    s += "<section><h2><b>";
    s += String(++n);
    s += "</b>";
    s += title;
    s += "</h2>";
  };
  auto hint = [&](const char *text) {
    s += "<p class='h'>";
    s += text;
    s += "</p>";
  };
  // <label>Label<input name value attrs></label>, value HTML-escaped
  auto field = [&](const char *label, const char *name, const String &value, const char *attrs) {
    s += "<label>";
    s += label;
    s += "<input name='";
    s += name;
    s += "' value='";
    s += esc(value);
    s += "' ";
    s += attrs;
    s += "></label>";
  };
  auto number = [&](const char *label, const char *name, int value, int lo, int hi) {
    s += "<label>";
    s += label;
    s += "<input name='";
    s += name;
    s += "' type='number' inputmode='numeric' value='";
    s += String(value);
    s += "' min='";
    s += String(lo);
    s += "' max='";
    s += String(hi);
    s += "'></label>";
  };
  auto option = [&](int value, bool selected, const char *text) {
    s += "<option value='";
    s += String(value);
    s += selected ? "' selected>" : "'>";
    s += text;
    s += "</option>";
  };

  // ===== HEADER =====
  s += "<header><h1>Tasky</h1><nav>";
  s += es ? "<a href='/?lang=en'>English</a> · <b>Español</b>"
          : "<b>English</b> · <a href='/?lang=es'>Español</a>";
  s += "</nav></header><p class='h'>Wi-Fi: ";
  if (net && net->isConnected()) {
    s += "<span class='ok'>";
    s += T("connected", "conectado");
    s += "</span> (";
    s += esc(net->getConnectedSSID());
    s += ", ";
    s += net->getIP();
    s += ")";
  } else {
    s += "<span class='er'>";
    s += T("not connected", "sin conexión");
    s += "</span>";
  }
  s += " · " + getUptimeString() + "</p>";
#if TASKY_MODE_DEV
  hint(T("Developer mode: advanced integrations", "Modo desarrollador: integraciones avanzadas"));
#endif

  s += "<form method='POST' action='/save'><input type='hidden' name='lang' value='";
  s += T("en", "es");
  s += "'>";

  // ===== 1 WI-FI =====
  section("Wi-Fi");
  hint(T("Tasky uses Wi-Fi for the time, weather and your calendar. 2.4 GHz networks only.",
         "Tasky usa Wi-Fi para la hora, el clima y tu calendario. Solo redes de 2.4 GHz."));
  field(T("Network", "Red"), "ssid", model->cfg.wifiSSID,
        es ? "maxlength='32' list='nets' autocomplete='off' placeholder='Nombre de la red'"
           : "maxlength='32' list='nets' autocomplete='off' placeholder='Network name'");
  {
    // Networks found by the background scan started in begin() (never blocks)
    int found = WiFi.scanComplete();
    if (found > 0) {
      s += "<datalist id='nets'>";
      for (int i = 0; i < found && i < 20; i++) {
        String ssid = WiFi.SSID(i);
        bool dup = ssid.length() == 0;
        for (int j = 0; j < i && !dup; j++)
          dup = WiFi.SSID(j) == ssid;
        if (dup)
          continue;
        s += "<option value='" + esc(ssid) + "'>";
      }
      s += "</datalist>";
    }
  }
  s += secretField(T("Password", "Contraseña"), "pass", model->cfg.wifiPass,
                   T("Wi-Fi password", "Contraseña del Wi-Fi"));
  s += "<label>";
  s += T("Time zone (UTC offset, set from this phone)", "Zona horaria (UTC, se toma de este teléfono)");
  s += "<input id='tz' name='tz' type='number' value='" + String(model->cfg.timezone) +
       "' min='-12' max='14'></label></section>";

#if FEATURE_CALENDAR
  // ===== 2 CALENDAR =====
  section(T("Calendar", "Calendario"));
  hint(T("Paste your calendar's private iCal link. Today's events show up on Tasky, with a heads-up 5 minutes before each meeting.",
         "Pega el enlace iCal privado de tu calendario. Los eventos de hoy aparecen en Tasky, con un aviso 5 minutos antes de cada reunión."));
  hint(T("Where do I find it? Pick yours:", "¿Dónde lo encuentro? Elige el tuyo:"));
  auto guide = [&](const char *name, const char *s1, const char *s2, const char *s3) {
    s += "<details class='c'><summary>";
    s += name;
    s += "</summary><ol><li>";
    s += s1;
    s += "</li><li>";
    s += s2;
    s += "</li><li>";
    s += s3;
    s += "</li></ol></details>";
  };
  guide("Google",
        T("Open <a href='https://calendar.google.com/calendar/r/settings' target='_blank'>Google Calendar settings</a> on a computer.",
          "Abre la <a href='https://calendar.google.com/calendar/r/settings' target='_blank'>configuración de Google Calendar</a> en una computadora."),
        T("On the left, under <i>Settings for my calendars</i>, click your calendar.",
          "A la izquierda, en <i>Configuración de mis calendarios</i>, haz clic en tu calendario."),
        T("Scroll to <i>Integrate calendar</i> and copy <b>Secret address in iCal format</b>.",
          "Baja a <i>Integrar el calendario</i> y copia la <b>Dirección secreta en formato iCal</b>."));
  guide("iCloud",
        T("Open the Calendar app on your Mac or iPhone (or <a href='https://www.icloud.com/calendar/' target='_blank'>icloud.com/calendar</a>).",
          "Abre la app Calendario en tu Mac o iPhone (o <a href='https://www.icloud.com/calendar/' target='_blank'>icloud.com/calendar</a>)."),
        T("Tap the share icon next to the calendar and turn on <b>Public Calendar</b>.",
          "Toca el ícono de compartir junto al calendario y activa <b>Calendario público</b>."),
        T("Copy the link. It starts with <code>webcal://</code>; paste it as is.",
          "Copia el enlace. Empieza con <code>webcal://</code>; pégalo tal cual."));
  guide("Outlook",
        T("Open <a href='https://outlook.live.com/calendar/' target='_blank'>outlook.com</a> → <b>Settings</b> (gear) → <b>Calendar</b> → <b>Shared calendars</b>.",
          "Abre <a href='https://outlook.live.com/calendar/' target='_blank'>outlook.com</a> → <b>Configuración</b> (engrane) → <b>Calendario</b> → <b>Calendarios compartidos</b>."),
        T("Under <i>Publish a calendar</i>, pick your calendar and <i>Can view all details</i>, then <b>Publish</b>.",
          "En <i>Publicar un calendario</i>, elige tu calendario y <i>Puede ver todos los detalles</i>, luego <b>Publicar</b>."),
        T("Copy the <b>ICS</b> link.", "Copia el enlace <b>ICS</b>."));
  s += secretField(T("iCal link", "Enlace iCal"), "cal", model->cfg.calendarUrl,
                   "https://calendar.google.com/calendar/ical/…/basic.ics", "text");
  s += "</section>";
#endif

  // ===== 3 TASKS =====
  section(T("Today's three things", "Tus tres pendientes de hoy"));
  hint(T("Tasky keeps it to three. You can change them any time.",
         "Tasky se queda con tres. Puedes cambiarlos cuando quieras."));
  static const char *const TASK_PH_EN[] = {"Most important thing", "Second", "Third"};
  static const char *const TASK_PH_ES[] = {"Lo más importante", "Segundo", "Tercero"};
  for (int i = 0; i < 3; i++) {
    s += "<label>" + String(i + 1);
    if (model->taskDone[i]) {
      s += " <span class='ok'>✓ ";
      s += T("done", "hecho");
      s += "</span>";
    }
    s += "<input name='t" + String(i) + "' value='" + esc(model->tasks[i]) +
         "' maxlength='40' autocomplete='off' placeholder='";
    s += T(TASK_PH_EN[i], TASK_PH_ES[i]);
    s += "'></label>";
  }
  s += "<details class='c'><summary>";
  s += T("Notes (optional)", "Notas (opcional)");
  s += "</summary>";
  for (int i = 0; i < 3; i++) {
    s += "<label>" + String(i + 1) + "<textarea name='td" + String(i) + "' maxlength='200' rows='2'>" +
         esc(model->taskDescription[i]) + "</textarea></label>";
  }
  s += "</details></section>";

#if FEATURE_GITHUB
  // ===== 4 GITHUB =====
  section("GitHub");
  field(T("GitHub username", "Usuario de GitHub"), "ghu", model->cfg.githubUser,
        es ? "autocomplete='off' autocapitalize='off' placeholder='tu-usuario'"
           : "autocomplete='off' autocapitalize='off' placeholder='your-username'");
  hint(T("Your public contributions fill the GitHub page of the Streak screen.",
         "Tus contribuciones públicas llenan la página de GitHub de la pantalla Streak."));
  s += "</section>";
#endif

#if FEATURE_WEATHER
  // ===== 5 WEATHER =====
  section(T("Weather", "Clima"));
  field(T("City", "Ciudad"), "wCity", model->cfg.weatherCity,
        es ? "autocomplete='off' placeholder='Ciudad de México'" : "autocomplete='off' placeholder='Mexico City'");
  hint(T("Tasky finds it and shows today's weather. No API key needed.",
         "Tasky la ubica y muestra el clima de hoy. Sin API key."));
  s += "<label>";
  s += T("Units", "Unidades");
  s += "<select name='wCel'>";
  option(1, model->cfg.weatherCelsius, "Celsius");
  option(0, !model->cfg.weatherCelsius, "Fahrenheit");
  s += "</select></label></section>";
#endif

  // ===== 6 PHRASES =====
  section(T("Your phrases", "Tus frases"));
  hint(T("Shown on Home and on the Phrases screen. One per line (up to 12, 60 characters each); add an author after a dash: <code>Stay hungry - Steve Jobs</code>. Leave it empty for the built-in ones.",
         "Salen en Home y en la pantalla Phrases. Una por línea (hasta 12, de 60 caracteres); agrega el autor después de un guion: <code>Paso a paso - Anónimo</code>. Déjalo vacío para usar las que trae Tasky."));
  s += "<textarea name='ph' rows='6' maxlength='800' placeholder='";
  s += T("One thing at a time&#10;Deep work, calm mind", "Una cosa a la vez&#10;Enfoque y calma");
  s += "'>" + esc(model->cfg.phrases) + "</textarea><p class='h'>";
  s += T("Accents are removed: the screen font is plain ASCII.",
         "Los acentos se quitan: la pantalla solo tiene letras sin acento.");
  s += "</p></section>";

  // ===== MORE (advanced) =====
  s += "<details class='mo'><summary>";
  s += T("More", "Más");
  s += "</summary>";
  number(T("Minutes per focus session", "Minutos por sesión de enfoque"), "work", model->cfg.workMin, 5, 90);
  number(T("Workday starts (hour, 0-23)", "Inicio de jornada (hora, 0-23)"), "dsh", model->cfg.dayStartHour, 0, 23);
  number(T("Workday ends (hour, 0-23)", "Fin de jornada (hora, 0-23)"), "deh", model->cfg.dayEndHour, 0, 23);
  hint(T("Tasky gives you a summary when the day starts and ends. Focus mode (light + lock while a session runs) is set on Tasky: Settings → More → Focus.",
         "Tasky te da un resumen al empezar y al cerrar el día. El modo Focus (luz y bloqueo durante la sesión) se ajusta en Tasky: Settings → More → Focus."));

#if FEATURE_WEATHER
  s += "<label>";
  s += T("Latitude / longitude (0 = find by city)", "Latitud / longitud (0 = buscar por ciudad)");
  s += "<input name='wLat' type='number' step='0.0001' value='" + String(model->cfg.weatherLat, 4) + "'>";
  s += "<input name='wLon' type='number' step='0.0001' value='" + String(model->cfg.weatherLon, 4) + "'></label>";
#endif

#if FEATURE_LLM
  s += "<label>";
  s += T("AI brief", "Resumen con IA");
  s += "<select name='lPrv'>";
  option(1, model->cfg.llmProvider == 1, "ChatGPT (OpenAI)");
  option(0, model->cfg.llmProvider == 0, "Claude (Anthropic)");
  s += "</select></label>";
  s += secretField(T("API key", "API key"), "lKey", model->cfg.llmApiKey, "sk-…");
  hint(T("Optional. Every morning Tasky writes your day in three lines (what matters, what to start with, a tip) from your calendar and tasks.",
         "Opcional. Cada mañana Tasky resume tu día en tres líneas (lo importante, por dónde empezar, un consejo) con tu calendario y tus tareas."));
  hint(T("Get one at <a href='https://platform.openai.com/api-keys' target='_blank'>platform.openai.com</a> or <a href='https://console.anthropic.com/settings/keys' target='_blank'>console.anthropic.com</a>. The key stays on your Tasky.",
         "Consíguela en <a href='https://platform.openai.com/api-keys' target='_blank'>platform.openai.com</a> o <a href='https://console.anthropic.com/settings/keys' target='_blank'>console.anthropic.com</a>. La key se queda en tu Tasky."));
#endif

#if FEATURE_NOTION && TASKY_MODE_DEV
  // Notion (dev mode only: needs an API key)
  hint(T("Notion: create an integration at <a href='https://www.notion.so/my-integrations' target='_blank'>notion.so/my-integrations</a>, share your task database with it and paste the database ID (32 characters from its URL). Properties: Name, Status, Due.",
         "Notion: crea una integración en <a href='https://www.notion.so/my-integrations' target='_blank'>notion.so/my-integrations</a>, comparte tu base de tareas con ella y pega el ID de la base (32 caracteres de su URL). Propiedades: Name, Status, Due."));
  s += secretField("Notion API key", "nkey", model->cfg.notionApiKey, "secret_...");
  field("Notion database ID", "ndb", model->cfg.notionDatabaseId, "autocomplete='off'");
#endif

#if FEATURE_FIREBASE
  // Firebase cloud sync
  hint(T("Cloud sync: <a href='https://console.firebase.google.com' target='_blank'>Firebase</a> Realtime Database URL and its database secret.",
         "Sincronización: URL de Realtime Database de <a href='https://console.firebase.google.com' target='_blank'>Firebase</a> y su secret."));
  field("Firebase URL", "fbUrl", model->cfg.firebaseUrl, "placeholder='project.firebaseio.com'");
  s += secretField("Firebase secret", "fbKey", model->cfg.firebaseSecret, "");
  s += "<label>Sync<select name='fbOn'>";
  option(1, model->cfg.firebaseEnabled, "ON");
  option(0, !model->cfg.firebaseEnabled, "OFF");
  s += "</select></label>";
#endif

#if FEATURE_SPOTIFY && TASKY_MODE_DEV
  // Spotify (dev mode only)
  hint(T("Spotify: create an app at <a href='https://developer.spotify.com/dashboard' target='_blank'>developer.spotify.com</a> with redirect URI",
         "Spotify: crea una app en <a href='https://developer.spotify.com/dashboard' target='_blank'>developer.spotify.com</a> con redirect URI"));
  s += "<code>http://" + WiFi.softAPIP().toString() + "/spotify/callback</code>";
  field("Spotify client ID", "spId", model->cfg.spotifyClientId, "autocomplete='off'");
  s += secretField("Spotify client secret", "spSec", model->cfg.spotifySecret, "");
  if (model->cfg.spotifyToken.length() > 0)
    s += "<p class='ok'>✓ Spotify</p>";
#endif
  s += "</details>";

  s += "<div class='sb'><button type='submit'>";
  s += T("Save", "Guardar");
  s += "</button></div></form>";

  // ===== FIRMWARE UPDATE (outside the settings form) =====
  s += "<details class='mo'><summary>";
  s += T("Firmware update", "Actualizar firmware");
  s += "</summary><p class='h'>";
  s += T("First, on Tasky: Settings → More → Update and press the knob. Updates stay enabled for 5 minutes.",
         "Primero en Tasky: Settings → More → Update y presiona la perilla. La actualización queda habilitada 5 minutos.");
  s += "</p><p>";
  s += T("Current version", "Versión actual");
  s += ": <b class='ok'>v" + (ota ? ota->getVersion() : String("?")) + "</b></p>";
  s += "<label>";
  s += T("Firmware file (.bin)", "Archivo de firmware (.bin)");
  s += "<input type='file' id='fw' accept='.bin'></label><div style='margin-top:12px'><button type='button' id='ub' onclick='up()'>";
  s += T("Update firmware", "Actualizar firmware");
  s += "</button></div><progress id='op' max='100' value='0' hidden></progress><p id='os' hidden></p></details>";

  // Send the phone's clock + time zone (no internet needed for the time)
  s += "<script>var z=Math.round(-new Date().getTimezoneOffset()/60),$=function(i){return document.getElementById(i)};"
       "$('tz').value=z;"
       "fetch('/time',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
       "body:'t='+Math.floor(Date.now()/1000)+'&tz='+z}).catch(function(){});";
  // Firmware upload: multipart POST to /ota/upload with progress
  s += "function up(){var f=$('fw').files[0],b=$('ub'),p=$('op'),o=$('os');"
       "if(!f||!/\\.bin$/.test(f.name)){alert('";
  s += T("Pick the .bin file", "Elige el archivo .bin");
  s += "');return}"
       "function fail(m){o.hidden=false;o.className='er';o.textContent='✗ '+m;b.disabled=false;b.textContent='";
  s += T("Update firmware", "Actualizar firmware");
  s += "'}"
       "var d=new FormData();d.append('firmware',f);b.disabled=true;b.textContent='";
  s += T("Uploading…", "Subiendo…");
  s += "';p.hidden=false;o.hidden=true;var x=new XMLHttpRequest();x.open('POST','/ota/upload',true);"
       "x.upload.onprogress=function(e){if(e.lengthComputable)p.value=Math.round(e.loaded/e.total*100)};"
       "x.onload=function(){try{var r=JSON.parse(x.responseText);if(r.success){o.hidden=false;o.className='ok';"
       "o.textContent='✓ '+r.message;b.textContent='";
  s += T("Restarting…", "Reiniciando…");
  s += "'}else fail('Error: '+r.message)}catch(e){fail('";
  s += T("Connection error", "Error de conexión");
  s += "')}};x.onerror=function(){fail('";
  s += T("Connection error", "Error de conexión");
  s += "')};x.send(d)}</script>";

  s += "<p class='h' style='text-align:center'>Tasky v" TASKY_VERSION
       " · <a href='https://github.com/arampersand/tasky' target='_blank'>GitHub</a></p></main></body></html>";
  return s;
}

String WiFiPortal::secretField(const char *label, const char *name, const String &stored,
                               const char *placeholder, const char *type) {
  bool isSet = stored.length() > 0;
  String s = "<label>" + String(label) + "<input name='" + String(name) + "' type='" + String(type) +
             "' value='' autocomplete='off' autocapitalize='off' placeholder='" +
             (isSet ? String(T("Saved. Leave empty to keep it", "Guardada. Déjalo vacío para conservarla"))
                    : esc(placeholder)) +
             "'></label>";
  if (isSet) {
    s += "<div class='sv'><span class='ok'>✓ ";
    s += T("Set", "Configurada");
    s += "</span><label style='margin:0'><input type='checkbox' name='" + String(name) + "_clr' value='1'>";
    s += T("clear", "borrar");
    s += "</label></div>";
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
  pickLang();
  server.send(200, "text/html; charset=utf-8", htmlPage());
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
  pickLang();
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

  String oldSSID = model->cfg.wifiSSID;
  String oldPass = model->cfg.wifiPass;
  if (server.hasArg("ssid"))
    model->cfg.wifiSSID = server.arg("ssid");
  applySecret("pass", model->cfg.wifiPass);
  bool wifiChanged = model->cfg.wifiSSID != oldSSID || model->cfg.wifiPass != oldPass;
  if (server.hasArg("tz"))
    model->cfg.timezone = (int8_t)server.arg("tz").toInt();

#if FEATURE_GITHUB
  if (server.hasArg("ghu")) {
    String ghu = server.arg("ghu");
    ghu.trim();
    model->cfg.githubUser = ghu;
  }
#endif
#if FEATURE_CALENDAR
  // Write-only like applySecret(), plus: pasted links lose spaces/line breaks,
  // webcal:// becomes https://, and typing "clear"/"borrar" also clears it.
  if (server.hasArg("cal_clr")) {
    model->cfg.calendarUrl = "";
  } else if (server.hasArg("cal")) {
    String raw = server.arg("cal"), cal;
    for (size_t i = 0; i < raw.length(); i++)
      if (!isspace((unsigned char)raw[i]))
        cal += raw[i];
    String low = cal;
    low.toLowerCase();
    if (low == "clear" || low == "borrar")
      model->cfg.calendarUrl = "";
    else if (cal.length() > 0)
      model->cfg.calendarUrl = low.startsWith("webcal://") ? "https://" + cal.substring(9) : cal;
  }
#endif

  // Phrases: one per line, ASCII-folded, up to 12 x 60 chars ("" = built-in)
  if (server.hasArg("ph")) {
    const String raw = server.arg("ph");
    String all;
    int count = 0;
    for (int pos = 0; pos <= (int)raw.length() && count < MAX_PHRASES;) {
      int nl = raw.indexOf('\n', pos);
      if (nl < 0)
        nl = raw.length();
      String ph = asciiFold(raw.substring(pos, nl));
      pos = nl + 1;
      if (ph.length() == 0)
        continue;
      if (ph.length() > MAX_PHRASE_LEN) {
        ph = ph.substring(0, MAX_PHRASE_LEN);
        ph.trim();
      }
      all += (count++ ? "\n" : "") + ph;
    }
    model->cfg.phrases = all;
  }

  // Notion Integration
#if FEATURE_NOTION
  applySecret("nkey", model->cfg.notionApiKey);
  if (server.hasArg("ndb"))
    model->cfg.notionDatabaseId = server.arg("ndb");
#endif

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
    if (server.hasArg("wCity")) {
      String city = server.arg("wCity");
      city.trim();
      model->cfg.weatherCity = city;
    }
    if (server.hasArg("wLat"))
      model->cfg.weatherLat = server.arg("wLat").toFloat();
    if (server.hasArg("wLon"))
      model->cfg.weatherLon = server.arg("wLon").toFloat();
    if (server.hasArg("wCel"))
      model->cfg.weatherCelsius = server.arg("wCel") == "1";
    // New city: drop the old coordinates so it re-geocodes (like usb_setup)
    if (oldCity != model->cfg.weatherCity) {
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

  // Reconnect only for new Wi-Fi details, or to retry while offline
  bool reconnect = net && (wifiChanged || !net->isConnected());
  if (reconnect) {
    net->disconnect(); // Clear old state
    net->begin();      // Start new connection attempt
  }

  String s;
  pageHead(s);
  s += "<h1 class='ok'>✓ ";
  s += T("Saved", "Guardado");
  s += "</h1><p>";
  if (reconnect && model->cfg.wifiSSID.length() > 0) {
    s += T("Tasky is connecting to ", "Tasky se está conectando a ");
    s += "<b>" + esc(model->cfg.wifiSSID) + "</b>…</p><p class='h'>";
    s += T("Watch Tasky's screen for the result. Your phone may leave the Tasky network.",
           "Mira la pantalla de Tasky para ver el resultado. Tu teléfono puede salir de la red Tasky.");
  } else {
    s += T("Tasky has your changes.", "Tasky ya tiene tus cambios.");
  }
  s += "</p><p><a href='/?lang=";
  s += T("en", "es");
  s += "'>";
  s += T("← Back", "← Volver");
  s += "</a></p></main></body></html>";
  server.send(200, "text/html; charset=utf-8", s);
}
