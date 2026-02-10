#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Callback for streaming line-by-line processing
// Return false to stop reading early
typedef bool (*LineCallback)(const char* line, void* userData);

class ApiClient {
public:
  ApiClient();

  // Performs a basic GET request with optional Bearer token
  // Returns payload or empty string on failure
  // WARNING: Loads entire response in RAM - avoid for large files
  String get(String url, String token = "");

  // Streaming GET - processes response line by line
  // Much more memory efficient for large files (iCal, etc.)
  // Returns true if completed successfully
  bool getStreaming(const String& url, LineCallback callback, void* userData, const String& token = "");

  // POST request with JSON body (for APIs like Notion)
  // Returns payload or empty string on failure
  String post(const String& url, const String& body, const String& token = "",
              const String& contentType = "application/json",
              const String& extraHeader = "", const String& extraHeaderValue = "");

  void setInsecure(); // Call this to skip cert validation (easier for prototype)

private:
  WiFiClientSecure client;
};
