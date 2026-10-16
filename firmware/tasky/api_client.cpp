#include "config_features.h"
#if FEATURE_CALENDAR || FEATURE_GITHUB || FEATURE_NOTION || FEATURE_WEATHER || FEATURE_FIREBASE
#include "api_client.h"
#include <WiFi.h>

String redactUrl(const String &url) {
  int scheme = url.indexOf("://");
  int start = scheme >= 0 ? scheme + 3 : 0;
  int slash = url.indexOf('/', start);
  return slash < 0 ? url : url.substring(0, slash) + "/…";
}

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
  Serial.println(redactUrl(url));

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

bool ApiClient::getStreaming(const String& url, LineCallback callback, void* userData, const String& token,
                             int maxBytes) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;

  Serial.print("[API] GET (streaming) ");
  Serial.println(redactUrl(url));

  http.begin(client, url);

  if (token.length() > 0) {
    http.addHeader("Authorization", "Bearer " + token);
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);  // 10 second connection timeout

  int code = http.GET();
  lastStatus = code;

  if (code != HTTP_CODE_OK && code != HTTP_CODE_MOVED_PERMANENTLY) {
    Serial.printf("[API] Stream error: %d\n", code);
    http.end();
    return false;
  }

  // Get the stream
  WiFiClient* stream = http.getStreamPtr();
  if (!stream) {
    Serial.println("[API] No stream");
    http.end();
    return false;
  }

  // SAFETY LIMITS - prevent infinite loops
  const int MAX_BYTES = maxBytes;     // 100 KB unless the caller needs more
  const uint32_t MAX_TIME_MS = 30000; // Max 30 seconds total
  const uint32_t IDLE_TIMEOUT_MS = 3000; // Max 3 seconds without data

  // Line buffer - 384 bytes max per line (iCal lines are short; a GitHub
  // contribution cell is ~300)
  const int LINE_BUF_SIZE = 384;
  char lineBuf[LINE_BUF_SIZE];
  int linePos = 0;
  bool success = true;
  int bytesRead = 0;
  int linesProcessed = 0;

  uint32_t startTime = millis();
  uint32_t lastDataTime = millis();

  // Read and process line by line with safety limits
  while ((http.connected() || stream->available()) &&
         bytesRead < MAX_BYTES &&
         (millis() - startTime) < MAX_TIME_MS) {

    if (stream->available()) {
      char c = stream->read();
      bytesRead++;
      lastDataTime = millis();  // Reset idle timer

      if (c == '\r') {
        continue;  // Skip CR
      }

      if (c == '\n') {
        // End of line - process it
        lineBuf[linePos] = '\0';

        if (linePos > 0) {
          linesProcessed++;
          if (!callback(lineBuf, userData)) {
            // Callback requested stop
            success = true;  // Not an error, just early termination
            break;
          }
        }
        linePos = 0;
      } else {
        // Add character to buffer
        if (linePos < LINE_BUF_SIZE - 1) {
          lineBuf[linePos++] = c;
        }
        // If line too long, just truncate silently
      }
    } else {
      // No data available
      if ((millis() - lastDataTime) > IDLE_TIMEOUT_MS) {
        Serial.println("[API] Stream idle timeout");
        break;
      }
      delay(1);  // Brief yield while waiting for data
    }

    // Yield frequently to prevent watchdog and allow other tasks
    if (bytesRead % 500 == 0) {
      yield();
    }
  }

  // Check why we exited
  if (bytesRead >= MAX_BYTES) {
    Serial.println("[API] Stream max bytes reached");
  }
  if ((millis() - startTime) >= MAX_TIME_MS) {
    Serial.println("[API] Stream max time reached");
  }

  // Process any remaining data in buffer
  if (linePos > 0) {
    lineBuf[linePos] = '\0';
    callback(lineBuf, userData);
  }

  Serial.printf("[API] Stream done: %d bytes, %d lines, %lu ms\n",
                bytesRead, linesProcessed, millis() - startTime);
  http.end();
  return success;
}

String ApiClient::post(const String& url, const String& body, const String& token,
                       const String& contentType, const String& extraHeader,
                       const String& extraHeaderValue) {
  if (WiFi.status() != WL_CONNECTED) {
    return "";
  }

  HTTPClient http;

  Serial.print("[API] POST ");
  Serial.println(redactUrl(url));

  http.begin(client, url);

  // Set content type
  http.addHeader("Content-Type", contentType);

  // Set authorization if provided
  if (token.length() > 0) {
    http.addHeader("Authorization", "Bearer " + token);
  }

  // Set extra header if provided (e.g., Notion-Version)
  if (extraHeader.length() > 0 && extraHeaderValue.length() > 0) {
    http.addHeader(extraHeader, extraHeaderValue);
  }

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(10000);

  int code = http.POST(body);
  String payload = "";

  if (code > 0) {
    Serial.printf("[API] POST Code: %d\n", code);
    if (code == HTTP_CODE_OK || code == HTTP_CODE_CREATED) {
      payload = http.getString();
    } else {
      Serial.printf("[API] POST Error response: %s\n", http.getString().c_str());
    }
  } else {
    Serial.printf("[API] POST Error: %s\n", http.errorToString(code).c_str());
  }

  http.end();
  return payload;
}
#endif
