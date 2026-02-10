#include "config_features.h"
#if FEATURE_SPOTIFY
#include "spotify_service.h"
#include <WiFi.h>

// Spotify App credentials - User needs to create their own app at developers.spotify.com
// For personal use, these can be hardcoded or configured via portal
const char* SpotifyService::CLIENT_ID = "";  // Set in portal
const char* SpotifyService::SCOPES = "user-read-playback-state user-modify-playback-state user-read-currently-playing";

SpotifyService::SpotifyService(TaskyModel *m) : model(m) {
  api.setInsecure();
  memset(&playback, 0, sizeof(playback));
}

void SpotifyService::begin() {
  if (!isConfigured()) return;
  forceUpdate();
}

void SpotifyService::loop() {
  if (!isConfigured()) return;

  // Update every 5 seconds for responsive controls
  if (millis() - lastUpdate > UPDATE_INTERVAL_MS) {
    forceUpdate();
  }
}

void SpotifyService::forceUpdate() {
  if (!isConfigured()) return;

  lastUpdate = millis();

  // Get current playback state
  String url = "https://api.spotify.com/v1/me/player";

  // Build headers with Bearer token
  String authHeader = "Bearer " + model->cfg.spotifyToken;

  // Make GET request
  HTTPClient http;
  http.begin(url);
  http.addHeader("Authorization", authHeader);

  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    parsePlaybackResponse(payload);
    dataValid = true;
  } else if (httpCode == 204) {
    // No active playback
    playback.isActive = false;
    playback.isPlaying = false;
    dataValid = true;
  } else if (httpCode == 401) {
    // Token expired, try to refresh
    Serial.println("[Spotify] Token expired, refreshing...");
    if (refreshToken()) {
      forceUpdate();  // Retry with new token
    }
  } else {
    Serial.printf("[Spotify] Error: %d\n", httpCode);
    dataValid = false;
  }

  http.end();
}

bool SpotifyService::isConfigured() const {
  return model->cfg.spotifyToken.length() > 0;
}

bool SpotifyService::hasData() const {
  return dataValid;
}

void SpotifyService::parsePlaybackResponse(const String& json) {
  // Parse "is_playing" field
  playback.isPlaying = extractJsonBool(json, "is_playing");
  playback.isActive = true;

  // Parse progress and duration
  playback.progressMs = extractJsonInt(json, "progress_ms");

  // Find "item" object for track info
  int itemIdx = json.indexOf("\"item\":");
  if (itemIdx > 0) {
    String itemSection = json.substring(itemIdx);

    // Track name
    String name = extractJsonStr(itemSection, "name");
    strncpy(playback.trackName, name.c_str(), sizeof(playback.trackName) - 1);
    playback.trackName[sizeof(playback.trackName) - 1] = '\0';

    // Duration
    playback.durationMs = extractJsonInt(itemSection, "duration_ms");

    // Artist name (first artist in array)
    int artistsIdx = itemSection.indexOf("\"artists\":");
    if (artistsIdx > 0) {
      String artistName = extractJsonStr(itemSection, "name", artistsIdx + 10);
      strncpy(playback.artistName, artistName.c_str(), sizeof(playback.artistName) - 1);
      playback.artistName[sizeof(playback.artistName) - 1] = '\0';
    }

    // Album name
    int albumIdx = itemSection.indexOf("\"album\":");
    if (albumIdx > 0) {
      String albumName = extractJsonStr(itemSection, "name", albumIdx + 8);
      strncpy(playback.albumName, albumName.c_str(), sizeof(playback.albumName) - 1);
      playback.albumName[sizeof(playback.albumName) - 1] = '\0';
    }
  }

  // Find "device" object for volume
  int deviceIdx = json.indexOf("\"device\":");
  if (deviceIdx > 0) {
    playback.volumePercent = extractJsonInt(json, "volume_percent", deviceIdx);
  }

  Serial.printf("[Spotify] Now playing: %s - %s\n", playback.artistName, playback.trackName);
}

bool SpotifyService::play() {
  return callSpotifyApi("PUT", "https://api.spotify.com/v1/me/player/play");
}

bool SpotifyService::pause() {
  return callSpotifyApi("PUT", "https://api.spotify.com/v1/me/player/pause");
}

bool SpotifyService::next() {
  return callSpotifyApi("POST", "https://api.spotify.com/v1/me/player/next");
}

bool SpotifyService::previous() {
  return callSpotifyApi("POST", "https://api.spotify.com/v1/me/player/previous");
}

bool SpotifyService::setVolume(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  String url = "https://api.spotify.com/v1/me/player/volume?volume_percent=" + String(percent);
  return callSpotifyApi("PUT", url);
}

bool SpotifyService::callSpotifyApi(const String& method, const String& endpoint, const String& body) {
  if (!isConfigured()) return false;

  HTTPClient http;
  http.begin(endpoint);
  http.addHeader("Authorization", "Bearer " + model->cfg.spotifyToken);

  int httpCode;
  if (method == "PUT") {
    http.addHeader("Content-Length", "0");
    httpCode = http.PUT("");
  } else if (method == "POST") {
    http.addHeader("Content-Length", "0");
    httpCode = http.POST("");
  } else {
    httpCode = http.GET();
  }

  bool success = (httpCode >= 200 && httpCode < 300);

  if (httpCode == 401) {
    // Token expired
    Serial.println("[Spotify] Token expired during control");
    refreshToken();
  } else if (!success) {
    Serial.printf("[Spotify] API error: %d\n", httpCode);
  }

  http.end();
  return success;
}

String SpotifyService::getAuthUrl() const {
  // Generate authorization URL for user to visit
  String clientId = model->cfg.spotifyClientId;
  if (clientId.length() == 0) return "";

  String url = "https://accounts.spotify.com/authorize";
  url += "?response_type=code";
  url += "&client_id=" + clientId;
  url += "&scope=";
  url += SCOPES;
  url += "&redirect_uri=http://";
  url += WiFi.softAPIP().toString();
  url += "/spotify/callback";

  return url;
}

bool SpotifyService::exchangeCode(const String& code) {
  if (code.length() == 0) return false;
  if (model->cfg.spotifyClientId.length() == 0 || model->cfg.spotifySecret.length() == 0) {
    Serial.println("[Spotify] Client ID or Secret not configured");
    return false;
  }

  HTTPClient http;
  http.begin("https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Basic auth header with client credentials
  String auth = model->cfg.spotifyClientId + ":" + model->cfg.spotifySecret;
  String authEncoded = "";  // Base64 encoding would be needed here
  // For simplicity, we'll use the body-based approach

  String body = "grant_type=authorization_code";
  body += "&code=" + code;
  body += "&redirect_uri=http://" + WiFi.softAPIP().toString() + "/spotify/callback";
  body += "&client_id=" + model->cfg.spotifyClientId;
  body += "&client_secret=" + model->cfg.spotifySecret;

  int httpCode = http.POST(body);

  if (httpCode == 200) {
    String response = http.getString();

    // Parse tokens
    model->cfg.spotifyToken = extractJsonStr(response, "access_token");
    model->cfg.spotifyRefresh = extractJsonStr(response, "refresh_token");
    model->save();

    Serial.println("[Spotify] Successfully authenticated!");
    http.end();
    return true;
  }

  Serial.printf("[Spotify] Token exchange failed: %d\n", httpCode);
  http.end();
  return false;
}

bool SpotifyService::refreshToken() {
  if (model->cfg.spotifyRefresh.length() == 0) {
    Serial.println("[Spotify] No refresh token available");
    return false;
  }

  HTTPClient http;
  http.begin("https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "grant_type=refresh_token";
  body += "&refresh_token=" + model->cfg.spotifyRefresh;
  body += "&client_id=" + model->cfg.spotifyClientId;
  body += "&client_secret=" + model->cfg.spotifySecret;

  int httpCode = http.POST(body);

  if (httpCode == 200) {
    String response = http.getString();

    // Parse new access token
    model->cfg.spotifyToken = extractJsonStr(response, "access_token");

    // Refresh token might be updated too
    String newRefresh = extractJsonStr(response, "refresh_token");
    if (newRefresh.length() > 0) {
      model->cfg.spotifyRefresh = newRefresh;
    }

    model->save();
    Serial.println("[Spotify] Token refreshed");
    http.end();
    return true;
  }

  Serial.printf("[Spotify] Token refresh failed: %d\n", httpCode);
  http.end();
  return false;
}

String SpotifyService::extractJsonStr(const String& json, const String& key, int startPos) {
  String searchKey = "\"" + key + "\":";
  int keyIdx = json.indexOf(searchKey, startPos);
  if (keyIdx < 0) return "";

  int valueStart = keyIdx + searchKey.length();
  // Skip whitespace
  while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
    valueStart++;
  }

  if (json[valueStart] == '"') {
    valueStart++;
    int valueEnd = json.indexOf('"', valueStart);
    if (valueEnd < 0) return "";
    return json.substring(valueStart, valueEnd);
  }
  return "";
}

int SpotifyService::extractJsonInt(const String& json, const String& key, int startPos) {
  String searchKey = "\"" + key + "\":";
  int keyIdx = json.indexOf(searchKey, startPos);
  if (keyIdx < 0) return 0;

  int valueStart = keyIdx + searchKey.length();
  while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
    valueStart++;
  }

  int valueEnd = valueStart;
  while (valueEnd < json.length() && (isdigit(json[valueEnd]) || json[valueEnd] == '-')) {
    valueEnd++;
  }

  if (valueEnd == valueStart) return 0;
  return json.substring(valueStart, valueEnd).toInt();
}

bool SpotifyService::extractJsonBool(const String& json, const String& key, int startPos) {
  String searchKey = "\"" + key + "\":";
  int keyIdx = json.indexOf(searchKey, startPos);
  if (keyIdx < 0) return false;

  int valueStart = keyIdx + searchKey.length();
  while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
    valueStart++;
  }

  return (json.substring(valueStart, valueStart + 4) == "true");
}
#endif
