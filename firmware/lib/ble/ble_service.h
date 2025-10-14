
#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <NimBLEDevice.h>
#include <Arduino.h>
#include <cstdint>

class BLEServerClass {
public:
  void begin();
  void transfer(uint8_t data[4]);

private:
  NimBLEServer* pServer = nullptr;
  NimBLECharacteristic* pCharacteristic = nullptr;
  bool deviceConnected = false;

  class ServerCallbacks : public NimBLEServerCallbacks {
  public:
    ServerCallbacks(BLEServerClass* parent) : pParent(parent) {}

    void onConnect(NimBLEServer* pServer) override {
      pParent->deviceConnected = true;
      Serial.println("BLE device connected.");
    }

    void onDisconnect(NimBLEServer* pServer) override {
      pParent->deviceConnected = false;
      Serial.println("BLE device disconnected.");
    }

  private:
    BLEServerClass* pParent;
  };

  ServerCallbacks serverCallbacks{this};
};

extern BLEServerClass bleServer;

#endif


