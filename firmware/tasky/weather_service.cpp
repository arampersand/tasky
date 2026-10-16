#include "config_features.h"
#if FEATURE_WEATHER
#include "weather_service.h"

WeatherService::WeatherService(TaskyModel *m) : model(m) {
  api.setInsecure();
}

void WeatherService::begin() {
  if (needsGeocode()) geocodeCity();
  if (model->cfg.weatherLat != 0 && model->cfg.weatherLon != 0) {
    // Copy city name for display
    if (model->cfg.weatherCity.length() > 0 && strlen(cityName) == 0) {
      strncpy(cityName, model->cfg.weatherCity.c_str(), sizeof(cityName) - 1);
      cityName[sizeof(cityName) - 1] = '\0';
    }
    forceUpdate();
  }
}

void WeatherService::loop() {
  if (!isConfigured()) return;

  // First update: wait 10s for WiFi, then every 30 minutes
  uint32_t interval = (lastUpdate == 0) ? 10000 : UPDATE_INTERVAL_MS;
  if (millis() - lastUpdate > interval) {
    forceUpdate();
  }
}

bool WeatherService::isConfigured() const {
  return (model->cfg.weatherLat != 0 && model->cfg.weatherLon != 0) ||
         model->cfg.weatherCity.length() > 0;
}

bool WeatherService::needsGeocode() const {
  return model->cfg.weatherCity.length() > 0 &&
         model->cfg.weatherLat == 0 && model->cfg.weatherLon == 0;
}

bool WeatherService::hasData() const {
  return dataValid;
}

// ============================================================
// JSON helpers (lightweight, no ArduinoJson)
// ============================================================

static String wJsonStr(const String& json, const String& key) {
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

static float wJsonNum(const String& json, const String& key) {
  String searchKey = "\"" + key + "\":";
  int keyIdx = json.indexOf(searchKey);
  if (keyIdx < 0) return 0;

  int valueStart = keyIdx + searchKey.length();
  while (valueStart < (int)json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t'))
    valueStart++;

  int valueEnd = valueStart;
  while (valueEnd < (int)json.length() &&
         (isdigit(json[valueEnd]) || json[valueEnd] == '.' || json[valueEnd] == '-'))
    valueEnd++;

  if (valueEnd == valueStart) return 0;
  return json.substring(valueStart, valueEnd).toFloat();
}

static int wJsonInt(const String& json, const String& key) {
  return (int)wJsonNum(json, key);
}

// ============================================================
// Geocoding: City name -> lat/lon via Open-Meteo
// ============================================================

void WeatherService::geocodeCity() {
  if (model->cfg.weatherCity.length() == 0) return;

  Serial.printf("[Weather] Geocoding city: %s\n", model->cfg.weatherCity.c_str());

  String url = "https://geocoding-api.open-meteo.com/v1/search?name=";
  // URL-encode city name (basic: replace spaces)
  String city = model->cfg.weatherCity;
  city.replace(" ", "%20");
  url += city;
  url += "&count=1&language=en";

  String response = api.get(url);
  if (response.length() == 0) {
    Serial.println("[Weather] Geocoding failed - no response");
    return;
  }

  // Parse: {"results":[{"latitude":19.4285,"longitude":-99.1277,"name":"Mexico City",...}]}
  int resultsIdx = response.indexOf("\"results\":");
  if (resultsIdx < 0) {
    Serial.println("[Weather] Geocoding - no results found");
    cityNotFound = true;
    return;
  }
  cityNotFound = false;

  String results = response.substring(resultsIdx);
  float lat = wJsonNum(results, "latitude");
  float lon = wJsonNum(results, "longitude");
  String name = wJsonStr(results, "name");

  if (lat != 0 || lon != 0) {
    model->cfg.weatherLat = lat;
    model->cfg.weatherLon = lon;
    model->save();

    if (name.length() > 0) {
      strncpy(cityName, name.c_str(), sizeof(cityName) - 1);
      cityName[sizeof(cityName) - 1] = '\0';
    }

    Serial.printf("[Weather] Geocoded: %s -> %.4f, %.4f\n", cityName, lat, lon);
  } else {
    Serial.println("[Weather] Geocoding returned invalid coordinates");
  }
}

// ============================================================
// Fetch weather from Open-Meteo
// ============================================================

void WeatherService::forceUpdate() {
  if (model->cfg.weatherLat == 0 && model->cfg.weatherLon == 0) {
    if (needsGeocode()) geocodeCity();
    if (model->cfg.weatherLat == 0) return;
  }

  Serial.println("[Weather] Updating from Open-Meteo...");

  String url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += String(model->cfg.weatherLat, 4);
  url += "&longitude=";
  url += String(model->cfg.weatherLon, 4);
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,is_day";
  url += "&temperature_unit=";
  url += model->cfg.weatherCelsius ? "celsius" : "fahrenheit";

  String response = api.get(url);

  if (response.length() > 0) {
    parseResponse(response);
    lastUpdate = millis();  // Only set on success - retry quickly on failure
    fetches++;
    failures = 0;
  } else {
    Serial.println("[Weather] Failed to fetch - will retry in 30s");
    failures++;
    dataValid = false;
    lastUpdate = millis() - UPDATE_INTERVAL_MS + 30000;  // Retry in 30s
  }
}

// ============================================================
// Parse Open-Meteo response
// ============================================================

void WeatherService::parseResponse(const String &json) {
  // Open-Meteo format:
  // {"current":{"temperature_2m":7.9,"relative_humidity_2m":70,
  //  "apparent_temperature":5.2,"weather_code":1,"is_day":1}}

  // Search for "current":{ to skip "current_units":{ which appears first
  int currentIdx = json.indexOf("\"current\":{");
  if (currentIdx < 0) {
    Serial.println("[Weather] No current data found");
    dataValid = false;
    return;
  }

  String current = json.substring(currentIdx);
  Serial.printf("[Weather] currentIdx=%d, parsing from: %.60s\n", currentIdx, current.c_str());

  temperature = wJsonNum(current, "temperature_2m");
  feelsLike = wJsonNum(current, "apparent_temperature");
  humidity = wJsonInt(current, "relative_humidity_2m");

  int weatherCode = wJsonInt(current, "weather_code");
  bool isDay = wJsonInt(current, "is_day") == 1;

  icon = mapWmoCode(weatherCode, isDay);

  // Generate description from WMO code
  const char* desc = "";
  switch (weatherCode) {
    case 0:  desc = isDay ? "Clear sky" : "Clear night"; break;
    case 1:  desc = "Mostly clear"; break;
    case 2:  desc = "Partly cloudy"; break;
    case 3:  desc = "Overcast"; break;
    case 45: case 48: desc = "Fog"; break;
    case 51: desc = "Light drizzle"; break;
    case 53: desc = "Drizzle"; break;
    case 55: desc = "Dense drizzle"; break;
    case 56: case 57: desc = "Freezing drizzle"; break;
    case 61: desc = "Light rain"; break;
    case 63: desc = "Rain"; break;
    case 65: desc = "Heavy rain"; break;
    case 66: case 67: desc = "Freezing rain"; break;
    case 71: desc = "Light snow"; break;
    case 73: desc = "Snow"; break;
    case 75: desc = "Heavy snow"; break;
    case 77: desc = "Snow grains"; break;
    case 80: desc = "Light showers"; break;
    case 81: desc = "Showers"; break;
    case 82: desc = "Heavy showers"; break;
    case 85: case 86: desc = "Snow showers"; break;
    case 95: desc = "Thunderstorm"; break;
    case 96: case 99: desc = "Thunderstorm w/ hail"; break;
    default: desc = "Unknown"; break;
  }
  strncpy(description, desc, sizeof(description) - 1);
  description[sizeof(description) - 1] = '\0';

  // City name from config (Open-Meteo doesn't return it)
  if (strlen(cityName) == 0 && model->cfg.weatherCity.length() > 0) {
    strncpy(cityName, model->cfg.weatherCity.c_str(), sizeof(cityName) - 1);
    cityName[sizeof(cityName) - 1] = '\0';
  }

  dataValid = true;
  Serial.printf("[Weather] %s: %.1f%c, %s (WMO %d)\n",
                cityName, temperature,
                model->cfg.weatherCelsius ? 'C' : 'F',
                description, weatherCode);
}

// ============================================================
// WMO weather code mapping
// https://www.noaa.gov/weather/wmo-weather-interpretation-codes
// ============================================================

WeatherIcon WeatherService::mapWmoCode(int code, bool isDay) {
  switch (code) {
    case 0:                              // Clear sky
      return isDay ? WEATHER_CLEAR_DAY : WEATHER_CLEAR_NIGHT;
    case 1: case 2: case 3:             // Partly cloudy / Overcast
      return WEATHER_CLOUDY;
    case 45: case 48:                    // Fog
      return WEATHER_MIST;
    case 51: case 53: case 55:           // Drizzle
    case 56: case 57:                    // Freezing drizzle
    case 61: case 63: case 65:           // Rain
    case 66: case 67:                    // Freezing rain
    case 80: case 81: case 82:           // Rain showers
      return WEATHER_RAIN;
    case 71: case 73: case 75: case 77:  // Snow
    case 85: case 86:                    // Snow showers
      return WEATHER_SNOW;
    case 95: case 96: case 99:           // Thunderstorm
      return WEATHER_THUNDER;
    default:
      return WEATHER_UNKNOWN;
  }
}
#endif
