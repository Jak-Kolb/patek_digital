#include <Arduino.h>
#include <string>
#include <vector>

#include "app_config.h"
#include "ble/ble_service.h"
#include "compute/consolidate.h"
#include "ringbuf/reg_buffer.h"
#include "storage/fs_store.h"
#include "wifi/wifi_mgr.h"

namespace {
uint32_t gLastProcess = 0;

void handle_command(const std::string& command) {
  if (command == kCmdList) {
    size_t bytes = fs_store::list();
    ble_service::notifyText(std::to_string(bytes));
    return;
  }

  if (command == kCmdSend) {
    const size_t total_size = fs_store::size();
    if (total_size == 0) {
      ble_service::notifyText("EMPTY");
      return;
    }

    Serial.printf("[BLE] Streaming %u bytes\n", static_cast<unsigned>(total_size));
    fs_store::read_chunks(0, total_size, [](const uint8_t* chunk, size_t length) {
      ble_service::notifyData(chunk, length);
      delay(20);  // Throttle to avoid overwhelming the BLE stack.
    });
    ble_service::notifyText("DONE");
    return;
  }

  if (command == kCmdErase) {
    if (fs_store::erase()) {
      ble_service::notifyText("ERASED");
    } else {
      ble_service::notifyText("ERASE_FAIL");
    }
    return;
  }

  Serial.print("[BLE] Unknown command: ");
  Serial.println(command.c_str());
  ble_service::notifyText("UNKNOWN");
}

void process_register_buffer() {
  uint8_t snapshot[kRegisterSize];
  regbuf_write_mock(nullptr, 0);  // Produce demo data until sensors are integrated.
  regbuf_snapshot(snapshot);

  std::vector<uint8_t> consolidated;
  consolidate(snapshot, kRegisterSize, consolidated);
  if (consolidated.empty()) {
    Serial.println("[MAIN] consolidate() produced empty payload.");
    return;
  }

  if (!fs_store::append(consolidated)) {
    Serial.println("[MAIN] Failed to append consolidated data to FS.");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("============================");
  Serial.println("ESP32 Data Node Boot");

  if (!fs_store::begin()) {
    Serial.println("[MAIN] Filesystem init failed.");
  }

  ble_service::begin(handle_command);

  if (wifi_mgr::begin()) {
    Serial.print("[MAIN] Wi-Fi active: ");
    Serial.println(wifi_mgr::ip_string());
  }
}

void loop() {
  const uint32_t now = millis();
  if (now - gLastProcess >= kLoopIntervalMs) {
    gLastProcess = now;
    process_register_buffer();
  }

  ble_service::loop();
  wifi_mgr::loop();
  delay(10);
}
