#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

class ApiClient {
public:
  ApiClient();

  // Performs a basic GET request with optional Bearer token
  // Returns payload or empty string on failure
  String get(String url, String token = "");

  void
  setInsecure(); // Call this to skip cert validation (easier for prototype)

private:
  WiFiClientSecure client;
};
