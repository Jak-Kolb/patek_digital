#include "ble_service.h"

// UUIDs for service and characteristic (customize as needed)
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServerClass bleServer;

void BLEServerClass::begin() {
  NimBLEDevice::init("ESP32_NimBLE_Server");
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  pCharacteristic->setValue("Init");

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
}

void BLEServerClass::transfer(uint8_t data[4]) {
  if (deviceConnected && pCharacteristic != nullptr) {
    pCharacteristic->setValue(data, 4);
    pCharacteristic->notify();
  }
}
