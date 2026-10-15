#include "config_features.h"
#if FEATURE_NOTION
#include "notion_service.h"
#include <Arduino.h>

NotionService::NotionService(TaskyModel* m) : model(m) {}

void NotionService::begin() {
  // Initial delay handled by lastUpdate = 0
}

void NotionService::loop() {
  // Skip if not configured
  if (model->cfg.wifiSSID.length() == 0 ||
      model->cfg.notionApiKey.length() == 0 ||
      model->cfg.notionDatabaseId.length() == 0) {
    return;
  }

  // Update every 10 minutes
  if (millis() - lastUpdate > 10 * 60 * 1000) {
    forceUpdate();
  }
}

void NotionService::forceUpdate() {
  if (model->cfg.notionApiKey.length() == 0 ||
      model->cfg.notionDatabaseId.length() == 0) {
    return;
  }

  Serial.println("[Notion] Updating...");
  lastUpdate = millis();

  // Clear existing tasks
  model->notionTaskCount = 0;
  memset(model->notionTasks, 0, sizeof(model->notionTasks));

  // Notion API endpoint
  String url = "https://api.notion.com/v1/databases/" +
               model->cfg.notionDatabaseId + "/query";

  // Empty filter body - get all items
  // You could add filters here like:
  // {"filter": {"property": "Status", "checkbox": {"equals": false}}}
  String body = "{}";

  // Make POST request with Notion-Version header
  String payload = api.post(url, body, model->cfg.notionApiKey,
                            "application/json",
                            "Notion-Version", "2022-06-28");

  if (payload.length() > 0) {
    parseResponse(payload);
    Serial.printf("[Notion] Found %d tasks\n", model->notionTaskCount);
  } else {
    Serial.println("[Notion] Request failed");
  }
}

void NotionService::parseResponse(const String& payload) {
  // Simple JSON parsing without ArduinoJson (saves memory)
  // Looking for pattern: "results": [ ... ]

  int resultsIdx = payload.indexOf("\"results\":");
  if (resultsIdx < 0) {
    Serial.println("[Notion] No results found");
    return;
  }

  int pos = resultsIdx;
  int taskIdx = 0;

  // Find each page object within results
  while (taskIdx < MAX_NOTION_TASKS) {
    // Find "properties" object
    int propsIdx = payload.indexOf("\"properties\":", pos);
    if (propsIdx < 0) break;

    // Find the end of this page object (next "object":"page" or end)
    int nextPageIdx = payload.indexOf("\"object\":\"page\"", propsIdx + 1);
    int endIdx = (nextPageIdx > 0) ? nextPageIdx : payload.length();

    // Extract the properties section for this page
    String propsSection = payload.substring(propsIdx, endIdx);

    // Look for title property (Name, Title, Task, etc.)
    // Pattern: "title":[{"text":{"content":"Task name"
    int titleIdx = propsSection.indexOf("\"title\":[");
    if (titleIdx > 0) {
      String titleStr = extractJsonString(propsSection, "\"content\":\"", titleIdx);
      if (titleStr.length() > 0) {
        strncpy(model->notionTasks[taskIdx].title, titleStr.c_str(), 32);
        model->notionTasks[taskIdx].title[32] = '\0';
      }
    }

    // Look for checkbox property (Done, Status, Completed, etc.)
    // Pattern: "checkbox":true or "checkbox":false
    int checkboxIdx = propsSection.indexOf("\"checkbox\":");
    if (checkboxIdx > 0) {
      model->notionTasks[taskIdx].done = extractJsonBool(propsSection, "\"checkbox\":", checkboxIdx);
    }

    // Look for description in rich_text
    int richTextIdx = propsSection.indexOf("\"rich_text\":[");
    if (richTextIdx > 0) {
      String descStr = extractJsonString(propsSection, "\"content\":\"", richTextIdx);
      if (descStr.length() > 0) {
        strncpy(model->notionTasks[taskIdx].description, descStr.c_str(), 64);
        model->notionTasks[taskIdx].description[64] = '\0';
      }
    }

    // Look for due date
    // Pattern: "date":{"start":"2024-12-20"
    int dateIdx = propsSection.indexOf("\"date\":{");
    if (dateIdx > 0) {
      String dateStr = extractJsonString(propsSection, "\"start\":\"", dateIdx);
      if (dateStr.length() >= 10) {
        strncpy(model->notionTasks[taskIdx].dueDate, dateStr.c_str(), 10);
        model->notionTasks[taskIdx].dueDate[10] = '\0';
      }
    }

    // Only count if we got a title
    if (model->notionTasks[taskIdx].title[0] != '\0') {
      taskIdx++;
    }

    pos = propsIdx + 1;
  }

  model->notionTaskCount = taskIdx;
}

String NotionService::extractJsonString(const String& json, const String& key, int startPos) {
  int keyIdx = json.indexOf(key, startPos);
  if (keyIdx < 0) return "";

  int valueStart = keyIdx + key.length();
  int valueEnd = json.indexOf("\"", valueStart);
  if (valueEnd < 0) return "";

  return json.substring(valueStart, valueEnd);
}

bool NotionService::extractJsonBool(const String& json, const String& key, int startPos) {
  int keyIdx = json.indexOf(key, startPos);
  if (keyIdx < 0) return false;

  int valueStart = keyIdx + key.length();
  // Check if next chars are "true"
  if (json.substring(valueStart, valueStart + 4) == "true") {
    return true;
  }
  return false;
}
#endif
