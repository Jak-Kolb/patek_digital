#include "ble_service.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include "app_config.h"

namespace ble_service {
namespace {
NimBLEServer* gServer = nullptr;
NimBLECharacteristic* gDataChar = nullptr;
ControlCommandCallback gControlCallback;

class ControlCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    if (!gControlCallback) {
      return;
    }
    std::string value = characteristic->getValue();
    if (value.empty()) {
      return;
    }
    gControlCallback(value);
  }
};

ControlCallbacks gControlCallbacks;

void configureAdvertising() {
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(kServiceUuid);
  advertising->setScanResponse(true);
  advertising->start();
}

}  // namespace

void begin(ControlCommandCallback callback) {
  if (gServer != nullptr) {
    return;  // Already initialized.
  }

  gControlCallback = callback;

  NimBLEDevice::init(kBleDeviceName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // Balanced TX power for dev boards.
  NimBLEDevice::setSecurityAuth(false, false, false);

  gServer = NimBLEDevice::createServer();
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
  return gServer->getConnectedCount() > 0;
}

void loop() {
  // Placeholder for future housekeeping (e.g., MTU negotiation or battery telemetry).
}

}  // namespace ble_service
