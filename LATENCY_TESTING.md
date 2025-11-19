# Bluetooth Latency Testing Guide

## Overview

The system now includes comprehensive latency tracking for BLE transfers, measuring performance on both the ESP32 (sender) and Python receiver (client).

---

## ESP32 Side (Firmware)

### Metrics Tracked

- **Total transfer time** - Complete duration from start to finish
- **Per-packet timing** - Individual notification send times
- **Average latency** - Mean time per packet
- **Throughput** - Packets/second and bytes/second

### Serial Monitor Output Example

```
[BLE] Starting transfer of 120 records
[BLE] Sent 10/10 packets, avg latency: 8 ms
[BLE] Sent 20/20 packets, avg latency: 7 ms
[BLE] Sent 30/30 packets, avg latency: 8 ms
...
[BLE] Transfer Statistics:
  Total time: 9240 ms
  Packets sent: 120
  Avg latency per packet: 7.83 ms
  Throughput: 12.99 packets/sec
  Data rate: 129.87 bytes/sec
```

### How to View

```bash
cd firmware
./scripts/monitor.sh
# Then trigger transfer from Python client
```

---

## Python Receiver Side

### Metrics Tracked

- **Total transfer time** - Time from SEND command to END marker
- **First packet latency** - Time until first data packet arrives
- **Inter-packet delays** - Time between consecutive packets (avg/min/max)
- **Throughput** - bytes/sec and bits/sec
- **Records per second**

### Terminal Output Example

```
================================================================================
BLE TRANSFER LATENCY STATISTICS
================================================================================
Total transfer time:      9.342 seconds
First packet latency:     0.142 seconds
Packets received:         120
Avg inter-packet delay:   75.23 ms
Min inter-packet delay:   62.18 ms
Max inter-packet delay:   98.45 ms
Throughput:               128.42 bytes/sec
                          1027.36 bits/sec
Records per second:       12.84
================================================================================
```

### How to Test

```bash
cd backend
python src/ble_receiver.py --sync-time
# Watch for latency statistics after transfer completes
```

---

## Understanding the Metrics

### Normal Performance Expectations

| Metric                   | Expected Range    | Notes                                 |
| ------------------------ | ----------------- | ------------------------------------- |
| **Per-packet latency**   | 5-15 ms           | ESP32 notification overhead           |
| **Inter-packet delay**   | 60-100 ms         | Includes 60ms vTaskDelay()            |
| **First packet latency** | 100-300 ms        | BLE connection + command processing   |
| **Throughput**           | 10-15 packets/sec | Limited by 60ms delay between packets |
| **Data rate**            | 100-150 bytes/sec | 10 bytes/record × throughput          |

### Factors Affecting Latency

1. **BLE Connection Interval** (7.5-4000ms)

   - Lower = faster, higher power
   - Negotiated automatically

2. **MTU Size** (23-517 bytes)

   - Larger MTU = fewer packets for same data
   - Current: Default ~20 bytes effective

3. **Inter-packet Delay** (configurable)

   - Current: 60ms (`vTaskDelay(pdMS_TO_TICKS(60))`)
   - Prevents overwhelming the BLE stack
   - Adjustable in `ble_service.cpp`

4. **Base64 Encoding Overhead**

   - 10 bytes → ~16 bytes encoded (~60% overhead)
   - Necessary for text-safe transmission

5. **Distance & Interference**
   - BLE range: ~10m indoors
   - 2.4GHz interference from WiFi/microwaves

---

## Optimizing Transfer Speed

### Option 1: Reduce Inter-Packet Delay

```cpp
// In ble_service.cpp, line ~158
vTaskDelay(pdMS_TO_TICKS(30));  // Change from 60 to 30
```

**Effect:** Doubles throughput (~25 packets/sec)
**Risk:** May cause packet loss if BLE stack can't keep up

### Option 2: Increase MTU

```cpp
// In ble_service.cpp, begin() function
NimBLEDevice::setMTU(185);  // Uncomment this line
```

**Effect:** Allows larger packets (more data per notification)
**Note:** Client must also support larger MTU

### Option 3: Disable Latency Logging

```cpp
// Comment out the per-packet timing in stream_all_records()
// Reduces Serial.printf overhead
```

**Effect:** Minor improvement (1-2%)

---

## Troubleshooting High Latency

### Symptom: Inter-packet delay > 200ms

**Causes:**

- Poor signal strength (move devices closer)
- 2.4GHz interference (disable WiFi on ESP32)
- BLE stack overload (increase inter-packet delay)

### Symptom: First packet latency > 1 second

**Causes:**

- BLE connection quality issues
- ESP32 filesystem access slow (check for LittleFS fragmentation)
- Python async event loop blocked

### Symptom: Packet loss (received < expected)

**Causes:**

- Inter-packet delay too short (increase from 60ms)
- MTU size mismatch
- Unreliable BLE connection

---

## Testing Different Scenarios

### 1. Baseline Test (Current Settings)

```bash
# ESP32: 60ms delay, default MTU
python src/ble_receiver.py --sync-time
```

### 2. High-Speed Test

```bash
# Change vTaskDelay to 30ms, rebuild firmware
./scripts/build_flash.sh
python src/ble_receiver.py --sync-time
```

### 3. Long-Distance Test

```bash
# Move devices 5-10 meters apart
python src/ble_receiver.py --sync-time
# Compare latency statistics
```

### 4. Stress Test (Large Dataset)

```bash
# Let ESP32 collect data for 1 hour (120 records)
# Then transfer
python src/ble_receiver.py --sync-time
```

---

## Logging to File

### ESP32 Serial Output

```bash
./scripts/monitor.sh | tee esp32_latency.log
```

### Python Output

```bash
python src/ble_receiver.py --sync-time 2>&1 | tee python_latency.log
```

---

## Expected Results Summary

With current settings (60ms inter-packet delay):

- **Transfer rate:** ~12-13 records/second
- **100 records:** ~8-9 seconds
- **1000 records:** ~80-90 seconds
- **Max throughput:** ~130 bytes/second

This is perfectly acceptable for health monitoring data that's transferred periodically (e.g., daily sync).

For real-time streaming, reduce inter-packet delay to 10-30ms for 3-5x improvement.
