#pragma once

#include "api_client.h"
#include "model.h"
#include <Arduino.h>
#include <Preferences.h>

// Agent states
enum AgentState {
  AGENT_IDLE = 0,  // Ready - showing today's mode
  AGENT_SENDING,   // Building & sending request
  AGENT_THINKING,  // Waiting for LLM response
  AGENT_DONE,      // Response ready to display
  AGENT_ERROR      // Config error (no API key)
};

// LLM Provider
enum LlmProvider {
  LLM_ANTHROPIC = 0,
  LLM_OPENAI = 1
};

// ROTATIVE MODE SYSTEM - 9 daily modes
enum LlmMode {
  MODE_FOCUS = 0,  // Enfoque - concrete action to advance
  MODE_START,      // Arranque - break inertia
  MODE_TIME,       // Tiempo - time optimization
  MODE_CLARITY,    // Claridad - prioritization question
  MODE_ORDER,      // Orden - physical order = mental clarity
  MODE_CLOSE,      // Cierre - end-of-day preparation
  MODE_MOVE,       // Movimiento - brief exercise routine
  MODE_GYM,        // Gym - day-specific gym routine
  MODE_RECIPES,    // Recetas - cheap recipe under $10
  LLM_MODE_COUNT   // = 9
};

// Rate limits (unchanged)
#define LLM_MAX_HOURLY 20
#define LLM_MAX_DAILY 100

// Response buffer (180 char target + margin)
#define LLM_RESPONSE_MAX 256
#define LLM_RESPONSE_LIMIT 180

class LlmService {
public:
  LlmService(TaskyModel* model);

  void begin();
  void loop();

  // Daily mode system
  void sendDailyPrompt();  // Auto-selects today's mode, checks cache first
  void cancel();
  void reset();

  // State
  AgentState getState() const {
    return state;
  }
  bool isConfigured() const;
  bool isRateLimited() const;

  // Mode info (for UI)
  LlmMode getTodayMode();
  static const char* getModeName(LlmMode mode);
  static const char* getModeDesc(LlmMode mode);

  // Mode override (-1=auto, 0-6=specific)
  void setModeOverride(int8_t mode);
  int8_t getModeOverride() const;

  // Response data
  char response[LLM_RESPONSE_MAX];
  int responseLen = 0;
  char errorMsg[64];

private:
  TaskyModel* model;
  ApiClient api;
  Preferences cachePref;  // Separate NVS namespace for cache

  AgentState state = AGENT_IDLE;
  uint32_t requestStartMs = 0;
  LlmMode todayMode = MODE_FOCUS;
  uint32_t todayDayIdx = 0;

  // Date & mode calculation
  uint32_t calculateDayIndex();
  LlmMode calculateMode();

  // NVS cache (per-mode: cr0-cr6)
  bool loadCachedResponse(LlmMode mode);
  void cacheResponse(LlmMode mode);
  void loadFallback();

  // Rate limiting (unchanged)
  bool checkRateLimit();
  void incrementRateCount();
  void resetHourlyIfNeeded();

  // Build request
  String buildSystemPrompt();
  String buildToolContext();
  String buildAnthropicRequest(const char* userMessage);
  String buildOpenAIRequest(const char* userMessage);

  // Parse response
  void parseAnthropicResponse(const String& json);
  void parseOpenAIResponse(const String& json);

  // Enforce character limit on response
  void enforceResponseLimit();

  // Perform the HTTP call (blocking)
  void executeRequest(const char* userMessage);
};
