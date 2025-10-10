#include "ble_service.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "app_config.h"

namespace ble_service {
namespace {
NimBLEServer* gServer = nullptr;
NimBLECharacteristic* gDataChar = nullptr;
ControlCommandCallback gControlCallback;
uint32_t gLedOffTime = 0;  // Time when LED should be turned off

// Forward declaration
void flashBlueLed();

class ControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    if (!gControlCallback) {
      return;
    }
    std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }
    
    // Flash LED to indicate command received
    flashBlueLed();
    
    gControlCallback(value);
  }
};

ControlCallbacks gControlCallbacks;

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server) override {
    Serial.println("[BLE] Client connected");
    // Flash LED to indicate connection
    flashBlueLed();
  }
  
  void onDisconnect(NimBLEServer* server) override {
    Serial.println("[BLE] Client disconnected");
    // Turn off LED immediately when disconnected
    digitalWrite(kBlueLedPin, LOW);  // Turn off LED (active low)
    gLedOffTime = 0;  // Cancel any pending LED off timer
  }
};

ServerCallbacks gServerCallbacks;

void configureAdvertising() {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setScanResponse(true);
  advertising->start();
}

void flashBlueLed() {
  digitalWrite(kBlueLedPin, LOW);  // Turn on LED (active low on most ESP32 boards)
  gLedOffTime = millis() + kLedFlashDurationMs;
}

void updateLed() {
  if (gLedOffTime > 0 && millis() >= gLedOffTime) {
    digitalWrite(kBlueLedPin, HIGH);  // Turn off LED (active low)
    gLedOffTime = 0;
  }
}

}  // namespace

void begin(ControlCommandCallback callback) {
  if (gServer != nullptr) {
    return;  // Already initialized.
  }

  gControlCallback = callback;

  // Initialize LED pin
  pinMode(kBlueLedPin, OUTPUT);
  digitalWrite(kBlueLedPin, HIGH);  // Turn off LED initially (active low)

  NimBLEDevice::init(kBleDeviceName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // Balanced TX power for dev boards.
  NimBLEDevice::setSecurityAuth(false, false, false);

  gServer = NimBLEDevice::createServer();
  gServer->setCallbacks(&gServerCallbacks);
  gServer->advertiseOnDisconnect(true);

  NimBLEService* service = gServer->createService(kServiceUuid);
  gDataChar = service->createCharacteristic(
      kDataCharUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  gDataChar->setCallbacks(nullptr);
  gDataChar->setValue("ready");

  NimBLECharacteristic* controlChar = service->createCharacteristic(
      kControlCharUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::READ);
  controlChar->setCallbacks(&gControlCallbacks);
  controlChar->setValue("idle");

  service->start();
  configureAdvertising();

  Serial.println("[BLE] Advertising as ESP32-DataNode");
}

void notifyData(const uint8_t* data, size_t length) {
  if (gDataChar == nullptr || data == nullptr || length == 0) {
    return;
  }
  if (!isClientConnected()) {
    return;
  }
  gDataChar->setValue(data, static_cast<uint16_t>(length));
  gDataChar->notify();
  
  // Flash LED to indicate BLE data transmission
  flashBlueLed();
}

void notifyText(const std::string& message) {
  if (message.empty()) {
    return;
  }
  notifyData(reinterpret_cast<const uint8_t*>(message.c_str()), message.size());
}

bool isClientConnected() {
  if (gServer == nullptr) {
    return false;
  }
  // NimBLEServer may exist but advertising might not yet be ready
  // Avoid calling getConnectedCount() too early (it can crash pre-init)
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv == nullptr || !adv->isAdvertising() || !NimBLEDevice::getInitialized()) {
    return false;
  }

  return gServer->getConnectedCount() > 0;
}

void loop() {
  // Update LED state for BLE activity indication
  updateLed();
  
  // Placeholder for future housekeeping (e.g., MTU negotiation or battery telemetry).
}

}  // namespace ble_service
