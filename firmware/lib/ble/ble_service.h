#ifndef BLE_SERVER_H
#define BLE_SERVER_H

#include <NimBLEDevice.h>
#include <Arduino.h>
#include <functional>

namespace consolidate { struct ConsolidatedRecord; }

// Inherit directly from callbacks to simplify structure
class BLEServerClass : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
public:
    void begin();
    void update(); // Call this in loop()

    // Public callbacks (assign these directly)
    std::function<void()> onErase;
    std::function<void(time_t)> onTimeSync;
    std::function<void()> onTransferStart;
    std::function<void()> onTransferComplete;

private:
    volatile bool _sendRequested = false;
    bool deviceConnected = false;
    NimBLECharacteristic* pNotifyCharacteristic = nullptr;

    // Overrides from NimBLEServerCallbacks
    void onConnect(NimBLEServer* pServer) override;
    void onDisconnect(NimBLEServer* pServer) override;

    // Overrides from NimBLECharacteristicCallbacks
    void onWrite(NimBLECharacteristic* characteristic) override;

    // Helpers
    void stream_all_records();
    bool notify(const uint8_t* data, size_t length);
};

extern BLEServerClass bleServer;

#endif