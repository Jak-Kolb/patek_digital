#pragma once

#include <stdint.h>
#include <iostream>
#include <Arduino.h>
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

// ===== Subsystem 1 (PCB Hardware Communication) =====
#define I2C_CLOCK_HZ            400000

// ESP32 DevKit V1 I2C pins
#define I2C_SDA_PIN             21
#define I2C_SCL_PIN             22

// 7-bit I2C addresses (adjust if your parts differ)
#define I2C_ADDR_MAX30102       0x57
#define I2C_ADDR_BMI270         0x68
#define I2C_ADDR_MAX30205       0x48

// Sampling cadences (ms)
#define PPG_INTERVAL_MS         20     // ~50 Hz
#define IMU_INTERVAL_MS         10     // ~100 Hz
#define TEMP_INTERVAL_MS        1000   // ~1 Hz

// Optional GPIO interrupt pins (set to actual pins if wired; leave -1 if not used)
#ifndef MAX30102_INT_PIN
#define MAX30102_INT_PIN        -1    // e.g., 19 if MAX30102 INT connected
#endif
#ifndef BMI270_INT_PIN
#define BMI270_INT_PIN          -1    // e.g., 18 if BMI270 INT1/INT2 connected
#endif

// Optional light sleep between interrupts (requires proper wake-capable pins and wiring)
#ifndef ENABLE_LIGHT_SLEEP
#define ENABLE_LIGHT_SLEEP      0
#endif

// Optional: integrate with your ring buffer
// #define SUB1_USE_RINGBUF 1

// ===== Ring buffer config (Subsystem 1 -> Subsystem 2) =====
#define REG_BUFFER_PAGE_BYTES   256     // fixed page size
#define REG_BUFFER_SLOTS        32      // total pages in ring (adjust as needed)


