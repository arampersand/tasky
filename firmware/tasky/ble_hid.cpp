#include "ble_hid.h"
#if FEATURE_BLE_HID
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

namespace {

// Report 1: boot-style keyboard (modifiers, reserved, 6 keys)
// Report 2: consumer control, one 16-bit usage
const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,              // Keyboard, id 1
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,  //   modifiers
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,                          //
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                          //   reserved
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,              //   6 keys
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,              //
    0xC0,                                                        //
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,              // Consumer, id 2
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,  //
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,                          //
    0xC0,
};

NimBLECharacteristic *keyboardReport = nullptr;
NimBLECharacteristic *consumerReport = nullptr;
volatile bool isConnected = false;
uint64_t peerAddr = 0;  // set on the NimBLE task: read and write under peerMux
portMUX_TYPE peerMux = portMUX_INITIALIZER_UNLOCKED;

// A peer we can recognize next time: its identity address. A private
// (resolvable) address changes every few minutes, so it counts as unknown.
uint64_t identityOf(const NimBLEConnInfo &info) {
  NimBLEAddress a = info.getIdAddress();
  if (a.getType() == BLE_ADDR_RANDOM && (a.getVal()[5] & 0xC0) == 0x40)
    return 0;
  return (uint64_t)a;
}

void setPeer(uint64_t a) {
  portENTER_CRITICAL(&peerMux);
  peerAddr = a;
  portEXIT_CRITICAL(&peerMux);
}

// The identity address is known once bonding resolves a phone's private one
class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override {
    setPeer(identityOf(info));
    isConnected = true;
  }
  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    setPeer(identityOf(info));
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    isConnected = false;
    setPeer(0);
  }
};

}  // namespace

namespace BleHid {

void begin(const char *name) {
  NimBLEDevice::init(name);
  NimBLEDevice::setSecurityAuth(true, false, true);  // bond, "just works", secure connections
  // Exchange identity keys when bonding, so a Mac or phone that changes its
  // Bluetooth address is still recognized (Controls remembers its mode)
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  server->advertiseOnDisconnect(true);

  NimBLEHIDDevice *hid = new NimBLEHIDDevice(server);
  keyboardReport = hid->getInputReport(1);
  consumerReport = hid->getInputReport(2);
  hid->setManufacturer("Tasky");
  hid->setPnp(0x02, 0x303A, 0x8001, 0x0100);  // USB-IF VID: Espressif
  hid->setHidInfo(0x00, 0x01);
  hid->setReportMap((uint8_t *)REPORT_MAP, sizeof(REPORT_MAP));
  hid->setBatteryLevel(100);
  server->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(hid->getHidService()->getUUID());
  adv->setName(name);
  adv->enableScanResponse(true);
  adv->start();
  Serial.println("[BLE] Advertising as a keyboard");
}

bool connected() { return isConnected; }
uint64_t peer() {
  if (!isConnected)
    return 0;
  portENTER_CRITICAL(&peerMux);
  uint64_t a = peerAddr;
  portEXIT_CRITICAL(&peerMux);
  return a;
}

void media(uint16_t usage) {
  if (!isConnected || !consumerReport)
    return;
  uint8_t press[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
  uint8_t release[2] = {0, 0};
  consumerReport->notify(press, sizeof(press));
  delay(8);
  consumerReport->notify(release, sizeof(release));
}

void keys(uint8_t modifiers, uint8_t key) {
  if (!isConnected || !keyboardReport)
    return;
  uint8_t press[8] = {modifiers, 0, key, 0, 0, 0, 0, 0};
  uint8_t release[8] = {0};
  keyboardReport->notify(press, sizeof(press));
  delay(8);
  keyboardReport->notify(release, sizeof(release));
}

}  // namespace BleHid
#endif
