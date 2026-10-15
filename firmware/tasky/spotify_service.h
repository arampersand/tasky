#pragma once

#include "api_client.h"
#include "model.h"
#include <Arduino.h>

// Spotify playback state
struct SpotifyPlayback {
  char trackName[48];
  char artistName[32];
  char albumName[32];
  uint32_t progressMs = 0;
  uint32_t durationMs = 0;
  bool isPlaying = false;
  int volumePercent = 50;
  bool isActive = false;  // Has active playback session
};

class SpotifyService {
public:
  SpotifyService(TaskyModel *model);

  void begin();
  void loop();
  void forceUpdate();

  bool isConfigured() const;
  bool hasData() const;

  // Playback controls
  bool play();
  bool pause();
  bool next();
  bool previous();
  bool setVolume(int percent);

  // Current playback state
  SpotifyPlayback playback;

  // OAuth helpers
  String getAuthUrl() const;
  bool exchangeCode(const String& code);
  bool refreshToken();

private:
  TaskyModel *model;
  ApiClient api;

  uint32_t lastUpdate = 0;
  bool dataValid = false;

  static const uint32_t UPDATE_INTERVAL_MS = 5000;  // 5 seconds for now playing

  void parsePlaybackResponse(const String& json);
  String extractJsonStr(const String& json, const String& key, int startPos = 0);
  int extractJsonInt(const String& json, const String& key, int startPos = 0);
  bool extractJsonBool(const String& json, const String& key, int startPos = 0);

  // Spotify API helpers
  bool callSpotifyApi(const String& method, const String& endpoint, const String& body = "");

  // OAuth constants
  static const char* CLIENT_ID;
  static const char* SCOPES;
};
