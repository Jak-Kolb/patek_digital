#include "ble_service.h"
#include "app_config.h"
#include "compute/consolidate.h"
#include "storage/fs_store.h"

BLEServerClass bleServer;

namespace {
    constexpr uint8_t kStartMarker = 0x01;
    constexpr uint8_t kDataMarker = 0x02;
    constexpr uint8_t kEndMarker = 0x03;
    constexpr const char kTimePrefix[] = "TIME:";
}

void BLEServerClass::begin() {
    NimBLEDevice::init(kBleDeviceName);
    NimBLEServer* pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(this); // We handle our own server events

    NimBLEService* pService = pServer->createService(kServiceUuid);

    pNotifyCharacteristic = pService->createCharacteristic(
        kDataCharUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic* pControl = pService->createCharacteristic(
        kControlCharUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    pControl->setCallbacks(this); // We handle our own char events

    pService->start();
    NimBLEDevice::getAdvertising()->addServiceUUID(kServiceUuid);
    NimBLEDevice::startAdvertising();

    // Serial.println("[BLE] Service Started");
}

// ============================================================================
// Event Handlers (Inherited)
// ============================================================================

void BLEServerClass::onConnect(NimBLEServer* pServer) {
    deviceConnected = true;
    // Serial.println("[BLE] Connected");
}

void BLEServerClass::onDisconnect(NimBLEServer* pServer) {
    deviceConnected = false;
    // Serial.println("[BLE] Disconnected");
}

void BLEServerClass::onWrite(NimBLECharacteristic* characteristic) {
    std::string val = characteristic->getValue();
    if (val.empty()) return;

    // Serial.printf("[BLE] Cmd: %s\n", val.c_str());

    if (val == kCmdSend) {
        _sendRequested = true; // Set flag for main loop
    } 
    else if (val == kCmdErase) {
        if (onErase) onErase();
        notify((uint8_t*)"ERASED", 6);
    } 
    else if (val.rfind(kTimePrefix, 0) == 0) { // Starts with TIME:
        long long epoch = atoll(val.c_str() + 5);
        if (epoch > 0 && onTimeSync) {
            onTimeSync((time_t)epoch);
            notify((uint8_t*)"TIME_OK", 7);
        }
    }
}

// ============================================================================
// Main Loop Logic
// ============================================================================

void BLEServerClass::update() {
    if (_sendRequested) {
        _sendRequested = false;
        stream_all_records();
    }
}

void BLEServerClass::stream_all_records() {
    if (!deviceConnected) return;
    if (onTransferStart) onTransferStart();

    size_t count = fs_store::record_count();
    // Serial.printf("[BLE] Streaming %u records...\n", count);

    // 1. Send Start
    uint8_t startBuf[5] = {kStartMarker};
    memcpy(&startBuf[1], &count, 4);
    notify(startBuf, 5);
    
    delay(50); 

    // 2. Send Data
    fs_store::for_each_record([this](const consolidate::ConsolidatedRecord& rec, size_t i) {
        if (!deviceConnected) return false;

        uint8_t packet[1 + sizeof(rec)];
        packet[0] = kDataMarker;
        memcpy(&packet[1], &rec, sizeof(rec));

        if (!notify(packet, sizeof(packet))) {
            // Serial.println("[BLE] Congestion drop");
        }

        delay(15); // Critical for flow control
        return true;
    });

    // 3. Send End
    uint8_t endMarker = kEndMarker;
    notify(&endMarker, 1);

    // Serial.println("[BLE] Done");
    if (onTransferComplete) onTransferComplete();
}

bool BLEServerClass::notify(const uint8_t* data, size_t length) {
    if (!deviceConnected || !pNotifyCharacteristic) return false;
    pNotifyCharacteristic->setValue(data, length);
    pNotifyCharacteristic->notify();
    return true;
}