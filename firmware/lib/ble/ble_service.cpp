#include "ble_service.h"

#include <cstdio>
#include <cstdlib>
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

bool equals(const std::string& value, const char* literal) {
  return value == literal;
}

}  // namespace

BLEServerClass bleServer;

void BLEServerClass::begin() {
  // NimBLEDevice::setMTU(185);
  NimBLEDevice::init(kBleDeviceName);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(&serverCallbacks);

  NimBLEService* pService = pServer->createService(kServiceUuid);

  pNotifyCharacteristic = pService->createCharacteristic(
      kDataCharUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pNotifyCharacteristic->setCallbacks(&notifyCallbacks);

  pControlCharacteristic = pService->createCharacteristic(
      kControlCharUuid,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pControlCharacteristic->setCallbacks(&controlCallbacks);

  const char init_msg[] = "READY";
  pNotifyCharacteristic->setValue(init_msg);

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

bool BLEServerClass::notify(const uint8_t* data, size_t length) {
  if (!deviceConnected || pNotifyCharacteristic == nullptr || data == nullptr || length == 0) {
    return false;
  }

  if (pNotifyCharacteristic->getSubscribedCount() == 0) {
    Serial.println("[BLE] Notify requested but no central is subscribed");
    return false;
  }

  if (!wait_for_notify_complete(200u)) {
    Serial.println("[BLE] Previous notification still pending, aborting new send");
    return false;
  }

  notifyPending = true;
  lastNotifyOk = true;

  pNotifyCharacteristic->setValue(data, length);
  pNotifyCharacteristic->notify();

  if (!wait_for_notify_complete(200u)) {
    Serial.println("[BLE] Notify timed out awaiting completion");
    return false;
  }

  return lastNotifyOk;
}

bool BLEServerClass::notify_string(const std::string& payload) {
  return notify(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
}

void BLEServerClass::ControlCallbacks::onWrite(NimBLECharacteristic* characteristic) {
  const std::string value = characteristic->getValue();
  if (value.empty()) {
    return;
  }
  Serial.printf("[BLE] Command received: %s\n", value.c_str());
  pParent->handle_command(value);
}

void BLEServerClass::handle_command(const std::string& command) {
  if (equals(command, kCmdSend)) {
    stream_all_records();
    return;
  }

  if (equals(command, kCmdErase)) {
    if (onErase_) {
      onErase_();
    }
    send_ack("ERASED");
    return;
  }

  if (equals(command, kCmdList)) {
    char buffer[16];
    const unsigned count = static_cast<unsigned>(fs_store::record_count());
    snprintf(buffer, sizeof(buffer), "L%u", count);
    notify(reinterpret_cast<const uint8_t*>(buffer), strlen(buffer));
    return;
  }

  if (command.rfind(kTimePrefix, 0) == 0) {
    const char* payload = command.c_str() + strlen(kTimePrefix);
    const long long epoch = atoll(payload);
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

void BLEServerClass::stream_all_records() {
  if (!deviceConnected || pNotifyCharacteristic == nullptr) {
    Serial.println("[BLE] Transfer requested but no central is connected");
    send_ack("NOCONN");
    return;
  }

  const size_t count = fs_store::record_count();

  if (onTransferStart_) {
    onTransferStart_();
  }

  char start_msg[16];
  snprintf(start_msg, sizeof(start_msg), "%c%u", kStartMarker, static_cast<unsigned>(count));
  notify(reinterpret_cast<const uint8_t*>(start_msg), strlen(start_msg));

  if (count == 0) {
    const uint8_t end_marker = static_cast<uint8_t>(kEndMarker);
    notify(&end_marker, 1);
    if (onTransferComplete_) {
      onTransferComplete_();
    }
    return;
  }

  bool aborted = false;
  fs_store::for_each_record(
      [this, &aborted](const consolidate::ConsolidatedRecord& record, size_t index) {
        (void)index;
        if (!send_record_packet(record)) {
          Serial.println("[BLE] Aborting transfer after notify failure");
          aborted = true;
          return false;
        }
        vTaskDelay(pdMS_TO_TICKS(60)); // Add a delay to allow the BLE stack to process
        return true;
      });

  if (aborted) {
    send_ack("STREAMERR");
    if (onTransferComplete_) {
      onTransferComplete_();
    }
    return;
  }

  const uint8_t end_marker = static_cast<uint8_t>(kEndMarker);
  notify(&end_marker, 1);

  if (onTransferComplete_) {
    onTransferComplete_();
  }
}

bool BLEServerClass::send_record_packet(const consolidate::ConsolidatedRecord& record) {
  unsigned char encoded[24] = {0};
  size_t out_len = 0;
  const int rc = mbedtls_base64_encode(encoded,
                                       sizeof(encoded),
                                       &out_len,
                                       reinterpret_cast<const unsigned char*>(&record),
                                       sizeof(record));
  if (rc != 0 || out_len == 0 || out_len > (sizeof(encoded))) {
    Serial.println("[BLE] Failed to encode record");
    return false;
  }

  uint8_t packet[20];
  const size_t total_len = out_len + 1;
  if (total_len > sizeof(packet)) {
    Serial.println("[BLE] Encoded packet too large");
    return false;
  }

  packet[0] = static_cast<uint8_t>(kDataMarker);
  memcpy(packet + 1, encoded, out_len);
  if (!notify(packet, total_len)) {
    Serial.println("[BLE] Failed to queue notify packet");
    return false;
  }

  return true;
}

void BLEServerClass::send_ack(const char* label) {
  char buffer[20] = {0};
  snprintf(buffer, sizeof(buffer), "%c%s", kAckMarker, label ? label : "");
  notify(reinterpret_cast<const uint8_t*>(buffer), strlen(buffer));
}

bool BLEServerClass::wait_for_notify_complete(uint32_t timeout_ms) {
  if (!notifyPending) {
    return true;
  }

  const uint32_t start = millis();
  while (notifyPending) {
    delay(1);
    if ((millis() - start) >= timeout_ms) {
      notifyPending = false;
      lastNotifyOk = false;
      return false;
    }
  }
  return true;
}

void BLEServerClass::handle_notify_status(NimBLECharacteristicCallbacks::Status status, int code) {
  if (status != NimBLECharacteristicCallbacks::Status::SUCCESS_NOTIFY &&
      status != NimBLECharacteristicCallbacks::Status::SUCCESS_INDICATE) {
    lastNotifyOk = false;
    Serial.printf("[BLE] notify status=%d code=%d\n", static_cast<int>(status), code);
  }
  if (code != 0) {
    lastNotifyOk = false;
    Serial.printf("[BLE] notify error code=%d\n", code);
  }

  notifyPending = false;
}
