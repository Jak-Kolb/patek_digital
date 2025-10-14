#include <Arduino.h>
#include <string>
#include <vector>

#include "app_config.h"
#include "wifi/wifi_mgr.h"
#include "ringbuf/reg_buffer.h"
#include "compute/consolidate.h"
#include "storage/fs_store.h"

uint8_t buffer[256];

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("============================");
  Serial.println("ESP32 Data Node Boot");
  Serial.println("============================");

  // Initialize filesystem first
  if (!fs_store::begin(true)) { // format on fail is true
    Serial.println("[MAIN] Filesystem init failed.");
    return; // Don't continue if filesystem fails
  }
  else {
    Serial.println("[MAIN] Filesystem initialized successfully.");
  }

  // Show current data file stats

  // Attempt WiFi connection first and show detailed status
  wifi_mgr::begin();
  delay(1000);

  // reg_buffer::generate_random_256_bytes(buffer, 256);
  // Serial.println("Generated 256 bytes of random data:");
  // for (size_t i = 0; i < 256; ++i) {
  //   Serial.printf("%d ", buffer[i]);
  // }
  // Serial.println();

}

void loop() {
  // const uint32_t now = millis();
  // if (now - gLastProcess >= kLoopIntervalMs) {
  //   gLastProcess = now;
  //   // process_register_buffer();
  // }
  
  // ble_service::loop();

  if (wifi_mgr::tick()) {
    delay(20);
  }
  else {
    Serial.println("WiFi not connected, retrying...");
    delay(5000);  // Retry every 5 seconds if not connected
  }


  // working data generation and storage basic
  
  /*
  reg_buffer::generate_random_256_bytes(buffer, 256);

  Serial.println("Generated 256 bytes of random data:");
  for (size_t i = 0; i < 256; i++) {
    Serial.printf("%d ", buffer[i]);
  }
  int32_t out[4] = {0};

  consolidate::consolidate(buffer, 256, out);
  Serial.printf("\nConsolidated to: %d %d %d %d\n", out[0], out[1], out[2], out[3]);
  fs_store::append(out);
  fs_store::printData();
  */

  // Serial.println();

  delay(1000);
}


