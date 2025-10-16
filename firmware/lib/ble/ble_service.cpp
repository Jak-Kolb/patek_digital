#include "ble_service.h"
#include <cstdio>
#include <cstring>
#include <mbedtls/base64.h>
#include "app_config.h"
#include "compute/consolidate.h"
#include "storage/fs_store.h"

namespace {
  constexpr char kStartMarker = 'C';
  constexpr char kDataMarker = 'D';
  constexpr char kEndMarker = 'E';
  constexpr char kAckMarker = 'A';
  constexpr const char kTimePrefix[] = "TIME:";
}

BLEServerClass bleServer;

// ============================================================================
// Public Methods
// ============================================================================

void BLEServerClass::begin() {
  NimBLEDevice::init(kBleDeviceName);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  NimBLEService* pService = pServer->createService(kServiceUuid);

  pNotifyCharacteristic = pService->createCharacteristic(
      kDataCharUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  pControlCharacteristic = pService->createCharacteristic(
      kControlCharUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pControlCharacteristic->setCallbacks(&controlCallbacks);

  pNotifyCharacteristic->setValue("READY");
  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(kServiceUuid);
  pAdvertising->start();

  Serial.println("[BLE] Advertising started");
}

void BLEServerClass::set_erase_callback(const std::function<void()>& cb) {
  onErase_ = cb;
}

void BLEServerClass::set_time_sync_callback(const std::function<void(time_t)>& cb) {
  onTimeSync_ = cb;
}

void BLEServerClass::set_transfer_start_callback(const std::function<void()>& cb) {
  onTransferStart_ = cb;
}

void BLEServerClass::set_transfer_complete_callback(const std::function<void()>& cb) {
  onTransferComplete_ = cb;
}

// ============================================================================
// Connection Callbacks
// ============================================================================

void BLEServerClass::ServerCallbacks::onConnect(NimBLEServer* pServer) {
  pParent->deviceConnected = true;
  Serial.println("[BLE] Device connected");
}

void BLEServerClass::ServerCallbacks::onDisconnect(NimBLEServer* pServer) {
  pParent->deviceConnected = false;
  Serial.println("[BLE] Device disconnected");
}

// ============================================================================
// Command Handling
// ============================================================================

void BLEServerClass::ControlCallbacks::onWrite(NimBLECharacteristic* characteristic) {
  const std::string value = characteristic->getValue();
  if (!value.empty()) {
    Serial.printf("[BLE] Command: %s\n", value.c_str());
    pParent->handle_command(value);
  }
}

void BLEServerClass::handle_command(const std::string& command) {
  // SEND command - stream all records
  if (command == kCmdSend) {
    stream_all_records();
    return;
  }

  // ERASE command - clear filesystem
  if (command == kCmdErase) {
    if (onErase_) onErase_();
    send_ack("ERASED");
    return;
  }

  // LIST command - return record count
  if (command == kCmdList) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "L%u", 
             static_cast<unsigned>(fs_store::record_count()));
    notify(reinterpret_cast<const uint8_t*>(buffer), strlen(buffer));
    return;
  }

  // TIME: command - sync time
  if (command.rfind(kTimePrefix, 0) == 0) {
    const long long epoch = atoll(command.c_str() + strlen(kTimePrefix));
    if (epoch > 0 && onTimeSync_) {
      onTimeSync_(static_cast<time_t>(epoch));
      send_ack("TIME");
    } else {
      send_ack("TIMEERR");
    }
    return;
  }

  send_ack("UNKNOWN");
}

// ============================================================================
// Data Transfer
// ============================================================================

void BLEServerClass::stream_all_records() {
  if (!deviceConnected) {
    Serial.println("[BLE] Transfer failed: not connected");
    send_ack("NOCONN");
    return;
  }

  if (onTransferStart_) onTransferStart_();

  // Start timing
  const uint32_t start_ms = millis();
  
  // Get count and send start marker
  const size_t count = fs_store::record_count();
  Serial.printf("[BLE] Starting transfer of %u records\n", static_cast<unsigned>(count));

  char start_msg[16];
  snprintf(start_msg, sizeof(start_msg), "%c%u", kStartMarker, 
           static_cast<unsigned>(count));
  notify(reinterpret_cast<const uint8_t*>(start_msg), strlen(start_msg));

  // Small delay to ensure start marker is processed
  vTaskDelay(pdMS_TO_TICKS(10));

  // Stream each record
  if (count > 0) {
    bool success = true;
    size_t packets_sent = 0;
    uint32_t total_notify_time = 0;

    fs_store::for_each_record(
      [this, &success, &packets_sent, &total_notify_time](const consolidate::ConsolidatedRecord& record, size_t index) {
        const uint32_t packet_start = millis();
        
        Serial.printf("[BLE] Sending packet %u...\n", static_cast<unsigned>(index + 1));
        
        if (!send_record_packet(record)) {
          Serial.printf("[BLE] Transfer aborted at packet %u\n", static_cast<unsigned>(index + 1));
          success = false;
          return false;
        }
        
        Serial.printf("[BLE] Packet %u sent successfully\n", static_cast<unsigned>(index + 1));
        
        const uint32_t packet_time = millis() - packet_start;
        total_notify_time += packet_time;
        packets_sent++;
        
        // Log every 10 packets
        if (packets_sent % 10 == 0) {
          Serial.printf("[BLE] Sent %u/%u packets, avg latency: %u ms\n",
                       static_cast<unsigned>(packets_sent),
                       static_cast<unsigned>(index + 1),
                       static_cast<unsigned>(total_notify_time / packets_sent));
        }
        
        // Adaptive delay based on packet count
        // More aggressive delay for larger transfers to prevent buffer overflow
        if (packets_sent < 10) {
          vTaskDelay(pdMS_TO_TICKS(5));   // Fast for small transfers
        } else if (packets_sent < 50) {
          vTaskDelay(pdMS_TO_TICKS(10));  // Medium for moderate transfers
        } else {
          vTaskDelay(pdMS_TO_TICKS(15));  // Conservative for large transfers
        }
        
        return true;
      });

    if (!success) {
      send_ack("STREAMERR");
      if (onTransferComplete_) onTransferComplete_();
      return;
    }

    // Calculate transfer statistics
    const uint32_t total_ms = millis() - start_ms;
    const float avg_per_packet = packets_sent > 0 ? (float)total_notify_time / packets_sent : 0;
    const float throughput = packets_sent > 0 ? (float)packets_sent * 1000.0f / total_ms : 0;

    Serial.println("[BLE] Transfer Statistics:");
    Serial.printf("  Total time: %u ms\n", static_cast<unsigned>(total_ms));
    Serial.printf("  Packets sent: %u\n", static_cast<unsigned>(packets_sent));
    Serial.printf("  Avg latency per packet: %.2f ms\n", avg_per_packet);
    Serial.printf("  Throughput: %.2f packets/sec\n", throughput);
    Serial.printf("  Data rate: %.2f bytes/sec\n", throughput * 10.0f);
  }

  // Send end marker
  const uint8_t end_marker = static_cast<uint8_t>(kEndMarker);
  notify(&end_marker, 1);

  if (onTransferComplete_) onTransferComplete_();
}

bool BLEServerClass::send_record_packet(const consolidate::ConsolidatedRecord& record) {
  // Base64 encode the record
  unsigned char encoded[24] = {0};
  size_t out_len = 0;
  const int rc = mbedtls_base64_encode(
      encoded, sizeof(encoded), &out_len,
      reinterpret_cast<const unsigned char*>(&record),
      sizeof(record));

  if (rc != 0 || out_len == 0 || out_len > sizeof(encoded)) {
    Serial.println("[BLE] Encoding failed");
    return false;
  }

  // Build packet with data marker
  uint8_t packet[20];
  packet[0] = static_cast<uint8_t>(kDataMarker);
  memcpy(packet + 1, encoded, out_len);

  return notify(packet, out_len + 1);
}

void BLEServerClass::send_ack(const char* label) {
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%c%s", kAckMarker, label ? label : "");
  notify(reinterpret_cast<const uint8_t*>(buffer), strlen(buffer));
}

// ============================================================================
// Notification
// ============================================================================

bool BLEServerClass::notify(const uint8_t* data, size_t length) {
  if (!deviceConnected || !pNotifyCharacteristic || !data || length == 0) {
    return false;
  }

  if (pNotifyCharacteristic->getSubscribedCount() == 0) {
    Serial.println("[BLE] No subscribers");
    return false;
  }

  pNotifyCharacteristic->setValue(data, length);
  pNotifyCharacteristic->notify();
  return true;
}
