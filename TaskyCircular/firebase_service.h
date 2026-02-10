#pragma once

#include "api_client.h"
#include "model.h"
#include <Arduino.h>

// Daily summary for syncing to Firebase
struct DailySummary {
  char date[11];           // YYYY-MM-DD
  uint8_t pomodoroCount;
  uint16_t focusMinutes;
  uint8_t streakIntensity;
};

class FirebaseService {
public:
  FirebaseService(TaskyModel *model);

  void begin();
  void loop();         // Non-blocking sync check
  void forceSync();    // Immediate sync

  bool isConfigured() const;
  bool isSyncing() const;
  String getDeviceId() const;
  String getLastSyncTime() const;

private:
  TaskyModel *model;
  ApiClient api;

  uint32_t lastSync = 0;
  bool syncing = false;
  String deviceId;
  uint32_t lastSyncTimestamp = 0;

  // Offline queue (simple for now)
  static const int MAX_PENDING = 5;
  DailySummary pendingQueue[MAX_PENDING];
  int pendingCount = 0;

  // Sync intervals
  static const uint32_t SYNC_INTERVAL_MS = 15 * 60 * 1000;  // 15 minutes
  static const uint32_t RETRY_INTERVAL_MS = 2 * 60 * 1000;  // 2 minutes on failure

  void syncDeviceInfo();
  void syncDailySummary();
  void syncStats();
  void processPendingQueue();

  String generateDeviceId();  // Based on MAC address
  String buildFirebaseUrl(const String &path);
  bool firebasePut(const String &path, const String &json);
  bool firebasePost(const String &path, const String &json);
  String firebaseGet(const String &path);

  String getTodayDateString();
};
