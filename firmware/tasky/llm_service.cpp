#include "config_features.h"
#if FEATURE_LLM
#include "llm_service.h"
#include <WiFi.h>
#include <time.h>

// Simple JSON helper (self-contained)
static String llmJsonStr(const String& json, const String& key) {
  String searchKey = "\"" + key + "\":";
  int keyIdx = json.indexOf(searchKey);
  if (keyIdx < 0) return "";
  int valueStart = keyIdx + searchKey.length();
  while (valueStart < (int)json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t'))
    valueStart++;
  if (json[valueStart] == '"') {
    valueStart++;
    int valueEnd = json.indexOf('"', valueStart);
    if (valueEnd < 0) return "";
    return json.substring(valueStart, valueEnd);
  }
  return "";
}

// ============================================================
// ROTATIVE MODE SYSTEM - Mode definitions
// ============================================================

// Display names (short, for header)
static const char* modeNames[LLM_MODE_COUNT] = {
  "FOCUS",
  "KICKSTART",
  "TIME",
  "CLARITY",
  "ORDER",
  "CLOSING",
  "MOVEMENT",
  "GYM",
  "RECIPES",
  "TODAY"
};

// Short descriptions (for IDLE screen)
static const char* modeDescs[LLM_MODE_COUNT] = {
  "Concrete action to move forward",
  "Break the day's inertia",
  "Optimize your time usage",
  "A question to prioritize",
  "Physical order, mental clarity",
  "Prepare a good closing",
  "Short exercise routine",
  "Gym routine for today",
  "Easy affordable recipe",
  "Your day in three lines"
};

// Full prompts sent to LLM (reference user context injected via system prompt)
static const char* modePrompts[LLM_MODE_COUNT] = {
  // MODE_FOCUS
  "Basandote en mis tareas y estado actual, dame una sola accion concreta "
  "que pueda hacer en menos de 30 minutos para avanzar algo importante. "
  "Una sola frase corta.",

  // MODE_START
  "Dame una accion minima para empezar mi dia productivo basada en mis "
  "tareas pendientes. Debe ser tan simple que no requiera motivacion. "
  "Una sola frase.",

  // MODE_TIME
  "Mirando mi calendario y tareas, dame una recomendacion concreta para "
  "usar mejor mi tiempo hoy, enfocada en eliminar una perdida innecesaria. "
  "Una sola frase.",

  // MODE_CLARITY
  "Basandote en mis tareas y prioridades actuales, dame una pregunta breve "
  "que me ayude a priorizar mejor y tomar mejores decisiones hoy. "
  "Solo una pregunta corta.",

  // MODE_ORDER
  "Dame una accion simple de orden fisico que mejore mi claridad mental y "
  "enfoque para las tareas de hoy. Una sola frase concreta.",

  // MODE_CLOSE
  "Mirando lo que he avanzado hoy, dame una sola cosa que pueda preparar "
  "para cerrar el dia con tranquilidad y facilitar el dia de manana. "
  "Una sola frase.",

  // MODE_MOVE
  "Dame una rutina breve de ejercicio para hoy, 3-4 ejercicios que pueda "
  "hacer en 20 minutos. Respuesta muy compacta, usa separadores con punto medio.",

  // MODE_GYM
  "Dame una rutina de gym para hoy con 4-5 ejercicios, indicando series y "
  "repeticiones. Alterna grupos musculares segun el dia de la semana. "
  "Formato compacto con punto medio como separador.",

  // MODE_RECIPES
  "Dame una receta sencilla que cueste menos de 10 dolares, con ingredientes "
  "y pasos muy breves. Que sea rapida de preparar (menos de 30 min). "
  "Formato compacto, maximo 180 caracteres.",

  // MODE_TODAY (the daily brief)
  "Con mi calendario y mis tareas de hoy, dame mi dia en exactamente 3 frases "
  "muy cortas (maximo 6 palabras cada una) separadas por ' / ': lo mas "
  "importante de hoy, con que empezar, y un consejo para el dia."
};

// Offline fallback phrases (shown when no internet/API fails)
static const char* modeFallbacks[LLM_MODE_COUNT] = {
  "Elige tu tarea mas importante y dedicale 25 minutos sin distracciones.",
  "Abre tu proyecto y escribe una sola linea de codigo o una sola idea.",
  "Revisa tu calendario y elimina una cosa que no sea necesaria hoy.",
  "Que es lo unico que si lo termino hoy hara que el dia valga la pena?",
  "Despeja tu escritorio dejando solo lo esencial para trabajar.",
  "Escribe tres cosas que lograste hoy y una meta para manana.",
  "10 sentadillas, 10 lagartijas, 30s plancha. Repite 3 veces.",
  "Pecho y triceps: 4x10 press banca, 3x12 aperturas, 3x15 fondos, 3x12 extension triceps.",
  "Huevos revueltos con frijoles y tortilla. Costo: $3. Tiempo: 10 min.",
  "Elige tu tarea clave / Empieza con 25 min de foco / Revisa tu calendario"
};

// ============================================================
// Constructor & lifecycle
// ============================================================

LlmService::LlmService(TaskyModel *m) : model(m) {
  api.setInsecure();
  memset(response, 0, sizeof(response));
  memset(errorMsg, 0, sizeof(errorMsg));
}

void LlmService::begin() {
  state = LLM_IDLE;
  // Calculate today's mode on startup
  todayDayIdx = calculateDayIndex();
  todayMode = calculateMode();
}

void LlmService::loop() {
  resetHourlyIfNeeded();
}

bool LlmService::isConfigured() const {
  return model->cfg.llmApiKey.length() > 10;
}

bool LlmService::isRateLimited() const {
  return model->cfg.llmHourlyCount >= LLM_MAX_HOURLY ||
         model->cfg.llmDailyCount >= LLM_MAX_DAILY;
}

// ============================================================
// Mode info (static, for UI)
// ============================================================

const char* LlmService::getModeName(LlmMode mode) {
  if (mode < 0 || mode >= LLM_MODE_COUNT) return "";
  return modeNames[mode];
}

const char* LlmService::getModeDesc(LlmMode mode) {
  if (mode < 0 || mode >= LLM_MODE_COUNT) return "";
  return modeDescs[mode];
}

LlmMode LlmService::getTodayMode() {
  // Manual override takes priority
  if (model->cfg.llmModeOverride >= 0 && model->cfg.llmModeOverride < LLM_MODE_COUNT)
    return (LlmMode)model->cfg.llmModeOverride;

  // Recalculate in case day changed
  uint32_t newIdx = calculateDayIndex();
  if (newIdx != todayDayIdx) {
    todayDayIdx = newIdx;
    todayMode = calculateMode();
  }
  return todayMode;
}

void LlmService::setModeOverride(int8_t mode) {
  model->cfg.llmModeOverride = mode;
  model->save();
}

int8_t LlmService::getModeOverride() const {
  return model->cfg.llmModeOverride;
}

// ============================================================
// Date calculation & mode rotation
// ============================================================

uint32_t LlmService::calculateDayIndex() {
  time_t now = time(nullptr);
  // Check if NTP has synced (after year 2020)
  if (now < 1577836800) {
    // No valid time - use last known day index from cache
    cachePref.begin("llm", true);
    uint32_t cached = cachePref.getUInt("cd", 0);
    cachePref.end();
    return cached > 0 ? cached : 1;
  }

  struct tm t;
  localtime_r(&now, &t);

  // Deterministic day index: same day = same value, new day = new value
  return (uint32_t)((t.tm_year + 1900) * 372 + (t.tm_mon + 1) * 31 + t.tm_mday);
}

// Auto = the daily brief ("Today"). The other modes stay one press away in
// the mode picker (manual override).
LlmMode LlmService::calculateMode() {
  return MODE_TODAY;
}

// ============================================================
// NVS cache system
// ============================================================

bool LlmService::loadCachedResponse(LlmMode mode) {
  cachePref.begin("llm", true);
  uint32_t cachedDay = cachePref.getUInt("cd", 0);

  if (cachedDay != todayDayIdx || todayDayIdx == 0) {
    cachePref.end();
    return false;
  }

  // Per-mode cache key: "cr0", "cr1", ..., "cr6"
  char key[4];
  snprintf(key, sizeof(key), "cr%d", (int)mode);
  String cached = cachePref.getString(key, "");
  cachePref.end();

  if (cached.length() > 0) {
    int copyLen = min((int)cached.length(), LLM_RESPONSE_MAX - 1);
    strncpy(response, cached.c_str(), copyLen);
    response[copyLen] = '\0';
    responseLen = copyLen;
    Serial.printf("[LLM] Cache hit for mode %d (%s)\n", (int)mode, modeNames[mode]);
    return true;
  }
  return false;
}

void LlmService::cacheResponse(LlmMode mode) {
  cachePref.begin("llm", false);
  uint32_t cachedDay = cachePref.getUInt("cd", 0);

  // If day changed, clear all per-mode caches
  if (cachedDay != todayDayIdx) {
    for (int i = 0; i < LLM_MODE_COUNT; i++) {
      char k[4];
      snprintf(k, sizeof(k), "cr%d", i);
      cachePref.remove(k);
    }
  }

  cachePref.putUInt("cd", todayDayIdx);
  char key[4];
  snprintf(key, sizeof(key), "cr%d", (int)mode);
  cachePref.putString(key, String(response));
  cachePref.putUChar("lm", (uint8_t)mode);
  cachePref.end();
  Serial.printf("[LLM] Cached response for day %u, mode %d (%s)\n",
                todayDayIdx, (int)mode, modeNames[mode]);
}

void LlmService::loadFallback() {
  const char* fb = modeFallbacks[todayMode];
  strncpy(response, fb, LLM_RESPONSE_MAX - 1);
  response[LLM_RESPONSE_MAX - 1] = '\0';
  responseLen = strlen(response);
  usedFallback = true;
  Serial.println("[LLM] Using offline fallback");
  // Don't cache fallbacks - next time with internet, get real response
}

// ============================================================
// Main entry point - daily prompt
// ============================================================

void LlmService::sendDailyPrompt() {
  // Recalculate in case day changed since begin()
  todayDayIdx = calculateDayIndex();

  // Get current mode (respects manual override)
  LlmMode currentMode = getTodayMode();
  todayMode = currentMode;

  memset(response, 0, sizeof(response));
  responseLen = 0;
  memset(errorMsg, 0, sizeof(errorMsg));
  usedFallback = false;

  // 1. Check per-mode cache first
  if (loadCachedResponse(currentMode)) {
    state = LLM_DONE;
    return;
  }

  // 2. Config check (only real error shown to user)
  if (!isConfigured()) {
    state = LLM_ERROR;
    strncpy(errorMsg, "Set API key in portal", sizeof(errorMsg) - 1);
    return;
  }

  // 3. Rate limit → use fallback gracefully
  if (isRateLimited()) {
    loadFallback();
    state = LLM_DONE;
    return;
  }

  // 4. No WiFi → use fallback gracefully
  if (WiFi.status() != WL_CONNECTED) {
    loadFallback();
    state = LLM_DONE;
    return;
  }

  // 5. Make API call
  state = LLM_SENDING;
  requestStartMs = millis();
  executeRequest(modePrompts[currentMode]);

  // 6. If API failed, use fallback; if success, enforce limit & cache
  if (state == LLM_ERROR) {
    loadFallback();
    state = LLM_DONE;
  } else if (state == LLM_DONE) {
    enforceResponseLimit();
    cacheResponse(currentMode);
  }
}

void LlmService::cancel() {
  state = LLM_IDLE;
}

void LlmService::reset() {
  state = LLM_IDLE;
  memset(response, 0, sizeof(response));
  responseLen = 0;
}

// ============================================================
// Response enforcement
// ============================================================

void LlmService::enforceResponseLimit() {
  if (responseLen <= LLM_RESPONSE_LIMIT) return;

  // Truncate at last space before limit to avoid cutting mid-word
  int cutAt = LLM_RESPONSE_LIMIT;
  while (cutAt > 0 && response[cutAt] != ' ') cutAt--;
  if (cutAt == 0) cutAt = LLM_RESPONSE_LIMIT; // no space found, hard cut

  response[cutAt] = '\0';
  responseLen = cutAt;
  Serial.printf("[LLM] Response truncated to %d chars\n", responseLen);
}

// ============================================================
// Rate limiting (unchanged)
// ============================================================

bool LlmService::checkRateLimit() {
  resetHourlyIfNeeded();
  return !isRateLimited();
}

void LlmService::incrementRateCount() {
  model->cfg.llmHourlyCount++;
  model->cfg.llmDailyCount++;
}

void LlmService::resetHourlyIfNeeded() {
  uint32_t now = millis();
  if (model->cfg.llmHourStart == 0 || (now - model->cfg.llmHourStart) > 3600000UL) {
    model->cfg.llmHourlyCount = 0;
    model->cfg.llmHourStart = now;
  }
}

// ============================================================
// Context building (kept - this is the killer feature)
// ============================================================

String LlmService::buildToolContext() {
  String ctx = "";

  // Tasks
  ctx += "Tareas de hoy: ";
  int doneCount = 0;
  for (int i = 0; i < 3; i++) {
    if (model->tasks[i].length() > 0 && model->tasks[i] != "Empty") {
      ctx += String(i + 1) + ". " + model->tasks[i];
      if (model->taskDone[i]) {
        ctx += " [HECHO]";
        doneCount++;
      }
      ctx += "; ";
    }
  }
  if (doneCount == 0 && model->tasks[0] == "Empty") {
    ctx += "Sin tareas definidas. ";
  }

  // Timer status
  if (model->st.running) {
    int mm = model->remainingMs() / 60000;
    ctx += "Timer: activo (" + String(mm) + "m restantes, ";
    ctx += (model->phase == PHASE_WORK) ? "TRABAJO" : "DESCANSO";
    ctx += "). ";
  } else if (model->st.finished) {
    ctx += "Timer: sesion recien terminada. ";
  } else {
    ctx += "Timer: pausado. Trabajo=" + String(model->cfg.workMin) + "m. ";
  }

  // Streak
  int streak = model->calculateCurrentStreak();
  ctx += "Racha: " + String(streak) + " dias consecutivos. ";

  // Pomodoro sessions today
  ctx += "Sesiones hoy: " + String(model->cfg.dailyPomodoroCount) + ". ";

  // Total focus time
  uint32_t totalMin = model->cfg.totalFocusMin;
  ctx += "Enfoque total: " + String(totalMin / 60) + "h " + String(totalMin % 60) + "m. ";

  // Calendar events
  if (model->todayEventCount > 0) {
    ctx += "Calendario: ";
    for (int i = 0; i < model->todayEventCount && i < 3; i++) {
      CalendarEventItem& evt = model->todayEvents[i];
      ctx += String(evt.title);
      if (!evt.isAllDay) {
        ctx += " a las " + String(evt.startTime);
      }
      ctx += "; ";
    }
  }

  // Clock
  int h, m, s;
  model->getClockNow(h, m, s);
  char timeBuf[8];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h, m);
  ctx += "Hora: " + String(timeBuf) + ". ";

  return ctx;
}

String LlmService::buildSystemPrompt() {
  String sys = "Eres Tasky AI, un coach de productividad en un dispositivo pequeno. ";
  sys += "Responde SOLO en espanol. ";
  if (todayMode == MODE_TODAY)  // the daily brief: three short phrases
    sys += "Reglas estrictas: exactamente 3 frases muy cortas separadas por ' / ', "
           "maximo 180 caracteres en total, sin emojis, sin comillas, sin viñetas. ";
  else
    sys += "Reglas estrictas: maximo 180 caracteres, una sola frase, "
           "sin emojis, sin comillas, sin listas, sin viñetas. ";
  sys += "Se directo, especifico y accionable basandote en los datos reales del usuario. ";
  sys += "Contexto del usuario:\n";
  sys += buildToolContext();
  return sys;
}

// ============================================================
// Request building (providers unchanged)
// ============================================================

String LlmService::buildAnthropicRequest(const char* userMessage) {
  String sys = buildSystemPrompt();
  sys.replace("\"", "\\\"");
  sys.replace("\n", "\\n");

  String msg = String(userMessage);
  msg.replace("\"", "\\\"");

  String body = "{\"model\":\"claude-haiku-4-5-20251001\",\"max_tokens\":128,\"system\":\"";
  body += sys;
  body += "\",\"messages\":[{\"role\":\"user\",\"content\":\"";
  body += msg;
  body += "\"}]}";

  return body;
}

String LlmService::buildOpenAIRequest(const char* userMessage) {
  String sys = buildSystemPrompt();
  sys.replace("\"", "\\\"");
  sys.replace("\n", "\\n");

  String msg = String(userMessage);
  msg.replace("\"", "\\\"");

  String body = "{\"model\":\"gpt-4o-mini\",\"max_tokens\":128,\"messages\":[";
  body += "{\"role\":\"system\",\"content\":\"" + sys + "\"},";
  body += "{\"role\":\"user\",\"content\":\"" + msg + "\"}]}";

  return body;
}

// ============================================================
// Response parsing (unchanged logic)
// ============================================================

void LlmService::parseAnthropicResponse(const String& json) {
  int contentIdx = json.indexOf("\"text\":\"");
  if (contentIdx < 0) {
    String errType = llmJsonStr(json, "type");
    if (errType == "error") {
      String errMsg = llmJsonStr(json, "message");
      strncpy(errorMsg, errMsg.length() > 0 ? errMsg.c_str() : "API error", sizeof(errorMsg) - 1);
    } else {
      strncpy(errorMsg, "No response", sizeof(errorMsg) - 1);
    }
    state = LLM_ERROR;
    return;
  }

  int textStart = contentIdx + 8;
  int textEnd = textStart;
  bool escaped = false;
  while (textEnd < (int)json.length()) {
    char c = json[textEnd];
    if (escaped) { escaped = false; textEnd++; continue; }
    if (c == '\\') { escaped = true; textEnd++; continue; }
    if (c == '"') break;
    textEnd++;
  }

  String text = json.substring(textStart, textEnd);
  text.replace("\\n", "\n");     // Preserve newlines for formatting
  text.replace("\\\"", "\"");
  text.replace("\\'", "'");

  int copyLen = min((int)text.length(), LLM_RESPONSE_MAX - 1);
  strncpy(response, text.c_str(), copyLen);
  response[copyLen] = '\0';
  responseLen = copyLen;
  state = LLM_DONE;
}

void LlmService::parseOpenAIResponse(const String& json) {
  int contentIdx = json.indexOf("\"content\":\"");
  if (contentIdx < 0) {
    String errMsg = llmJsonStr(json, "message");
    strncpy(errorMsg, errMsg.length() > 0 ? errMsg.c_str() : "API error", sizeof(errorMsg) - 1);
    state = LLM_ERROR;
    return;
  }

  int textStart = contentIdx + 11;
  int textEnd = textStart;
  bool escaped = false;
  while (textEnd < (int)json.length()) {
    char c = json[textEnd];
    if (escaped) { escaped = false; textEnd++; continue; }
    if (c == '\\') { escaped = true; textEnd++; continue; }
    if (c == '"') break;
    textEnd++;
  }

  String text = json.substring(textStart, textEnd);
  text.replace("\\n", "\n");    // Preserve newlines for formatting
  text.replace("\\\"", "\"");

  int copyLen = min((int)text.length(), LLM_RESPONSE_MAX - 1);
  strncpy(response, text.c_str(), copyLen);
  response[copyLen] = '\0';
  responseLen = copyLen;
  state = LLM_DONE;
}

// ============================================================
// Execute HTTP request (blocking, provider logic unchanged)
// ============================================================

void LlmService::executeRequest(const char* userMessage) {
  state = LLM_THINKING;

  String url, body, token;
  bool isAnthropic = (model->cfg.llmProvider == LLM_ANTHROPIC);

  if (isAnthropic) {
    url = "https://api.anthropic.com/v1/messages";
    body = buildAnthropicRequest(userMessage);
    token = model->cfg.llmApiKey;
  } else {
    url = "https://api.openai.com/v1/chat/completions";
    body = buildOpenAIRequest(userMessage);
    token = model->cfg.llmApiKey;
  }

  Serial.printf("[LLM] Mode: %s | Sending to %s (%d bytes)...\n",
                modeNames[todayMode], isAnthropic ? "Anthropic" : "OpenAI", body.length());

  String result;
  if (isAnthropic) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(15);

    if (!client.connect("api.anthropic.com", 443)) {
      strncpy(errorMsg, "Connection failed", sizeof(errorMsg) - 1);
      state = LLM_ERROR;
      return;
    }

    client.println("POST /v1/messages HTTP/1.1");
    client.println("Host: api.anthropic.com");
    client.println("Content-Type: application/json");
    client.print("x-api-key: ");
    client.println(token);
    client.println("anthropic-version: 2023-06-01");
    client.print("Content-Length: ");
    client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);

    uint32_t timeout = millis() + 15000;
    while (client.connected() && millis() < timeout) {
      if (client.available()) break;
      delay(10);
      yield();
    }

    // Skip HTTP headers
    while (client.available() && millis() < timeout) {
      String line = client.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) break;
      yield();
    }

    // Read body
    result = "";
    while (client.available() && millis() < timeout && result.length() < 4096) {
      char c = client.read();
      result += c;
      if (result.length() % 256 == 0) yield();
    }
    client.stop();
  } else {
    result = api.post(url, body, token);
  }

  if (result.length() == 0) {
    strncpy(errorMsg, "Empty response", sizeof(errorMsg) - 1);
    state = LLM_ERROR;
    Serial.println("[LLM] Empty response");
    return;
  }

  Serial.printf("[LLM] Response: %d bytes\n", result.length());

  if (isAnthropic) {
    parseAnthropicResponse(result);
  } else {
    parseOpenAIResponse(result);
  }

  if (state == LLM_DONE) {
    incrementRateCount();
    Serial.printf("[LLM] Success. Rate: %d/hour, %d/day\n",
                  model->cfg.llmHourlyCount, model->cfg.llmDailyCount);
  }
}

#endif
