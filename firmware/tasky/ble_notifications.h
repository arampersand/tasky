#pragma once

#include "model.h"
#include <Arduino.h>

// BLE connection modes
enum BleMode {
  BLE_OFF = 0,        // BLE disabled, WiFi only
  BLE_NOTIFICATIONS,  // Receive phone notifications
  BLE_MEDIA_ONLY      // Media info only (lighter)
};

// BLE connection state
enum BleState {
  BLE_STATE_IDLE,
  BLE_STATE_ADVERTISING,
  BLE_STATE_CONNECTED,
  BLE_STATE_DISCONNECTED
};

// Notification types
enum NotificationType {
  NOTIF_MESSAGE = 0,    // SMS, WhatsApp, etc.
  NOTIF_CALL,           // Incoming call
  NOTIF_MEDIA,          // Media playback info
  NOTIF_CALENDAR,       // Calendar reminder
  NOTIF_OTHER           // Other notifications
};

// Media playback state (from phone)
struct BleMediaInfo {
  char trackName[48];
  char artistName[32];
  char albumName[32];
  uint32_t durationMs;
  uint32_t positionMs;
  bool isPlaying;
  uint8_t volume;       // 0-100
  uint32_t lastUpdate;  // millis() when updated
};

// Phone notification
struct BleNotification {
  char appName[24];       // "WhatsApp", "Messages", etc.
  char title[48];         // Sender name or notification title
  char message[96];       // Message preview
  NotificationType type;
  uint32_t timestamp;     // When received
  bool read;              // Has been viewed
};

// Notification queue
#define MAX_NOTIFICATIONS 5

class BleNotificationService {
public:
  BleNotificationService(TaskyModel* model);

  // Lifecycle
  void begin();
  void stop();
  void loop();

  // Mode control
  void setMode(BleMode mode);
  BleMode getMode() const { return mode; }
  BleState getState() const { return state; }
  bool isConnected() const { return state == BLE_STATE_CONNECTED; }

  // Media info (works with ANY music app)
  BleMediaInfo mediaInfo;
  bool hasMediaInfo() const;

  // Notifications
  BleNotification notifications[MAX_NOTIFICATIONS];
  int notificationCount = 0;
  int unreadCount = 0;

  void clearNotifications();
  void markAllRead();
  void addNotification(const BleNotification& notif);

  // Callbacks (called from BLE event handlers)
  void onConnect();
  void onDisconnect();
  void onMediaUpdate(const char* track, const char* artist, const char* album,
                     uint32_t duration, uint32_t position, bool playing);
  void onNotificationReceived(const char* app, const char* title,
                              const char* message, NotificationType type);

  // Device name for pairing
  String getDeviceName() const;

private:
  TaskyModel* model;
  BleMode mode = BLE_OFF;
  BleState state = BLE_STATE_IDLE;

  bool bleInitialized = false;
  uint32_t lastStateChange = 0;

  void initBLE();
  void deinitBLE();
  void startAdvertising();
  void stopAdvertising();

  // ANCS (Apple Notification Center Service) for iOS
  void setupANCS();

  // Android Media Control
  void setupAndroidMedia();
};

// Global instance (needed for BLE callbacks)
extern BleNotificationService* g_bleService;
