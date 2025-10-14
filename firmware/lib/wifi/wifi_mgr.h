#pragma once

#include <Arduino.h>

namespace wifi_mgr {

// Call periodically from loop() to maintain the connection.
    void begin();

    // Returns true when Wi-Fi is configured and connected.
    bool is_connected();
    bool tick();
}  // namespace wifi_mgr
