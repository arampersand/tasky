#include "api_client.h"
#include <WiFi.h>

ApiClient::ApiClient() {
  // By default, for simplicity on ESP32 without CA bundle management:
  client.setInsecure();
}

void ApiClient::setInsecure() { client.setInsecure(); }

String ApiClient::get(String url, String token) {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }

  HTTPClient http;

  Serial.print("[API] GET ");
  Serial.println(url);

  // Use the secure client
  http.begin(client, url);

  if (token.length() > 0) {
    http.addHeader("Authorization", "Bearer " + token);
  }

  // Follow redirects? GitHub/Google might redirect
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int code = http.GET();
  String payload = "";

  if (code > 0) {
    Serial.printf("[API] Code: %d\n", code);
    if (code == HTTP_CODE_OK || code == HTTP_CODE_MOVED_PERMANENTLY) {
      payload = http.getString();
    }
  } else {
    Serial.printf("[API] Error: %s\n", http.errorToString(code).c_str());
  }

  http.end();
  return payload;
}
