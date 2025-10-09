#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ble_service {

using ControlCommandCallback = std::function<void(const std::string& command)>;

// Initialize NimBLE, create the service/characteristics, and begin advertising.
void begin(ControlCommandCallback callback);

// Notify subscribed clients with binary data pulled from persistent storage.
void notifyData(const uint8_t* data, size_t length);

// Notify subscribed clients with a UTF-8 encoded status message.
void notifyText(const std::string& message);

// Returns true if at least one BLE central is connected.
bool isClientConnected();

// Pump BLE housekeeping logic. Safe to call from loop().
void loop();

}  // namespace ble_service
