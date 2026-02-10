#include "config_features.h"
#if FEATURE_WEATHER
#include "weather_service.h"

WeatherService::WeatherService(TaskyModel *m) : model(m) {
  api.setInsecure();
}

void WeatherService::begin() {
  if (!isConfigured()) return;
  forceUpdate();
}

void WeatherService::loop() {
  if (!isConfigured()) return;

  // Update every 30 minutes
  if (millis() - lastUpdate > UPDATE_INTERVAL_MS) {
    forceUpdate();
  }
}

void WeatherService::forceUpdate() {
  if (!isConfigured()) return;

  Serial.println("[Weather] Updating...");
  lastUpdate = millis();

  // Build OpenWeatherMap API URL
  String url = "https://api.openweathermap.org/data/2.5/weather?q=";
  url += model->cfg.weatherCity;
  url += "&appid=";
  url += model->cfg.weatherApiKey;
  url += "&units=";
  url += model->cfg.weatherCelsius ? "metric" : "imperial";

  String response = api.get(url);

  if (response.length() > 0) {
    parseResponse(response);
  } else {
    Serial.println("[Weather] Failed to fetch");
    dataValid = false;
  }
}

bool WeatherService::isConfigured() const {
  return model->cfg.weatherApiKey.length() > 0 &&
         model->cfg.weatherCity.length() > 0;
}

bool WeatherService::hasData() const {
  return dataValid;
}

// Helper to extract a string value from JSON
String extractJsonStr(const String& json, const String& key) {
  String searchKey = "\"" + key + "\":";
  int keyIdx = json.indexOf(searchKey);
  if (keyIdx < 0) return "";

  int valueStart = keyIdx + searchKey.length();
  // Skip whitespace
  while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
    valueStart++;
  }

  if (json[valueStart] == '"') {
    // String value
    valueStart++;
    int valueEnd = json.indexOf('"', valueStart);
    if (valueEnd < 0) return "";
    return json.substring(valueStart, valueEnd);
  }
  return "";
}

// Helper to extract a number value from JSON
float extractJsonNum(const String& json, const String& key) {
  String searchKey = "\"" + key + "\":";
  int keyIdx = json.indexOf(searchKey);
  if (keyIdx < 0) return 0;

  int valueStart = keyIdx + searchKey.length();
  // Skip whitespace
  while (valueStart < json.length() && (json[valueStart] == ' ' || json[valueStart] == '\t')) {
    valueStart++;
  }

  // Find end of number
  int valueEnd = valueStart;
  while (valueEnd < json.length() &&
         (isdigit(json[valueEnd]) || json[valueEnd] == '.' || json[valueEnd] == '-')) {
    valueEnd++;
  }

  if (valueEnd == valueStart) return 0;
  return json.substring(valueStart, valueEnd).toFloat();
}

// Helper to extract an int value from JSON
int extractJsonInt(const String& json, const String& key) {
  return (int)extractJsonNum(json, key);
}

void WeatherService::parseResponse(const String &json) {
  // Simple JSON parsing without ArduinoJson (saves memory)

  // Check for API error
  int cod = extractJsonInt(json, "cod");
  if (cod != 200 && cod != 0) {
    String msg = extractJsonStr(json, "message");
    Serial.printf("[Weather] API error: %s\n", msg.c_str());
    dataValid = false;
    return;
  }

  // Parse main weather data
  // Find "main" object
  int mainIdx = json.indexOf("\"main\":");
  if (mainIdx < 0) {
    Serial.println("[Weather] No main data found");
    dataValid = false;
    return;
  }

  // Extract from main section
  String mainSection = json.substring(mainIdx);
  temperature = extractJsonNum(mainSection, "temp");
  feelsLike = extractJsonNum(mainSection, "feels_like");
  humidity = extractJsonInt(mainSection, "humidity");

  // Parse weather condition
  // Find "weather" array (first element)
  int weatherIdx = json.indexOf("\"weather\":");
  if (weatherIdx > 0) {
    String weatherSection = json.substring(weatherIdx);
    int conditionCode = extractJsonInt(weatherSection, "id");
    String iconCode = extractJsonStr(weatherSection, "icon");
    bool isNight = iconCode.length() > 0 && iconCode[iconCode.length()-1] == 'n';
    icon = mapConditionCode(conditionCode, isNight);

    // Parse description
    String desc = extractJsonStr(weatherSection, "description");
    if (desc.length() > 0) {
      strncpy(description, desc.c_str(), sizeof(description) - 1);
      description[sizeof(description) - 1] = '\0';
      // Capitalize first letter
      if (description[0] >= 'a' && description[0] <= 'z') {
        description[0] -= 32;
      }
    }
  }

  // Parse city name
  String city = extractJsonStr(json, "name");
  if (city.length() > 0) {
    strncpy(cityName, city.c_str(), sizeof(cityName) - 1);
    cityName[sizeof(cityName) - 1] = '\0';
  }

  dataValid = true;
  Serial.printf("[Weather] %s: %.1f%c, %s\n",
                cityName, temperature,
                model->cfg.weatherCelsius ? 'C' : 'F',
                description);
}

WeatherIcon WeatherService::mapConditionCode(int code, bool isNight) {
  // OpenWeatherMap condition codes: https://openweathermap.org/weather-conditions

  // Thunderstorm (2xx)
  if (code >= 200 && code < 300) return WEATHER_THUNDER;

  // Drizzle (3xx) or Rain (5xx)
  if (code >= 300 && code < 400) return WEATHER_RAIN;
  if (code >= 500 && code < 600) return WEATHER_RAIN;

  // Snow (6xx)
  if (code >= 600 && code < 700) return WEATHER_SNOW;

  // Atmosphere (7xx) - mist, fog, etc.
  if (code >= 700 && code < 800) return WEATHER_MIST;

  // Clear (800)
  if (code == 800) return isNight ? WEATHER_CLEAR_NIGHT : WEATHER_CLEAR_DAY;

  // Clouds (80x)
  if (code > 800 && code < 900) return WEATHER_CLOUDY;

  return WEATHER_UNKNOWN;
}
#endif
