#pragma once

#include <stdint.h>

// General configuration values for the firmware application.

// Device identification
constexpr char kBleDeviceName[] = "ESP32-DataNode";

// BLE UUIDs (string literals consumed by NimBLE APIs)
constexpr char kServiceUuid[] = "12345678-1234-5678-1234-56789abc0000";
constexpr char kDataCharUuid[] = "12345678-1234-5678-1234-56789abc1001";
constexpr char kControlCharUuid[] = "12345678-1234-5678-1234-56789abc1002";

// Filesystem configuration
constexpr char kFsDataPath[] = "/consolidated.dat";
constexpr size_t kFsChunkSize = 200;  // chunk size used for BLE notifications

// Register buffer configuration
constexpr size_t kRegisterSize = 256;

// Wi-Fi configuration toggle
#define ENABLE_WIFI 1

// File operations
constexpr uint32_t kLoopIntervalMs = 5000;

// BLE command keywords
constexpr char kCmdList[] = "LIST";
constexpr char kCmdSend[] = "SEND";
constexpr char kCmdErase[] = "ERASE";

// LED configuration
constexpr int kBlueLedPin = 2;  // Onboard LED pin for most ESP32 dev boards
constexpr uint32_t kLedFlashDurationMs = 100;  // LED flash duration for BLE activity

