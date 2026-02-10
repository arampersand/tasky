#pragma once

#include "api_client.h"
#include "model.h"
#include <Arduino.h>

// Weather condition icons (simplified)
enum WeatherIcon {
  WEATHER_CLEAR_DAY = 0,
  WEATHER_CLEAR_NIGHT,
  WEATHER_CLOUDY,
  WEATHER_RAIN,
  WEATHER_THUNDER,
  WEATHER_SNOW,
  WEATHER_MIST,
  WEATHER_UNKNOWN
};

class WeatherService {
public:
  WeatherService(TaskyModel *model);

  void begin();
  void loop();
  void forceUpdate();

  bool isConfigured() const;
  bool hasData() const;

  // Current weather data
  float temperature = 0;
  float feelsLike = 0;
  uint8_t humidity = 0;
  WeatherIcon icon = WEATHER_UNKNOWN;
  char description[32] = "";
  char cityName[24] = "";

private:
  TaskyModel *model;
  ApiClient api;

  uint32_t lastUpdate = 0;
  bool dataValid = false;

  static const uint32_t UPDATE_INTERVAL_MS = 30 * 60 * 1000;  // 30 minutes

  void parseResponse(const String &json);
  WeatherIcon mapConditionCode(int code, bool isNight);
};
