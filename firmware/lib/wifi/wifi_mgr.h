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

// Returns the MAC address of the ESP32 WiFi interface.
String mac_address();

// Returns the SSID of the currently connected network (empty if not connected).
String connected_ssid();

}  // namespace wifi_mgr
