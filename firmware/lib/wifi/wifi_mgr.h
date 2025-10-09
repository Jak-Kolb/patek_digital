#pragma once

#include <Arduino.h>

namespace wifi_mgr {

// Initialize Wi-Fi and attempt a connection if credentials are present.
bool begin();

// Call periodically from loop() to maintain the connection.
void loop();

// Disconnect from Wi-Fi. Safe to call even if not connected.
void disconnect();

// Returns true when Wi-Fi is configured and connected.
bool is_connected();

// Returns true if credentials were compiled into the firmware.
bool has_credentials();

// String view of current IP address (empty string if not connected).
String ip_string();

}  // namespace wifi_mgr
