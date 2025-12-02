#include <Arduino.h>
#include <ctime>
#include <string>
#include <vector>
#include <sys/time.h>

#include "app_config.h"
#include "wifi/wifi_mgr.h"
#include "ringbuf/reg_buffer.h"
#include "compute/consolidate.h"
#include "storage/fs_store.h"
// #include "compute/mockdata.h"
#include "ble/ble_service.h"
#include "sensors.h"

namespace {

volatile uint32_t gFallbackBaseMillis = 0;
volatile bool gResetRingRequested = false;
reg_buffer::SampleRingBuffer gRing;

void reset_fallback_clock() {
  gFallbackBaseMillis = millis();
}

void handle_ble_erase() {
  Serial.println("[BLE] Erase command received");
  if (fs_store::erase()) {
    Serial.println("[BLE] Filesystem data cleared");
  } else {
    Serial.println("[BLE] Filesystem erase failed");
  }
  reset_fallback_clock();
  gResetRingRequested = true;
}

void handle_ble_time_sync(time_t epoch) {
  Serial.printf("[BLE] Time sync epoch=%lld\n", static_cast<long long>(epoch));
  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  reset_fallback_clock();
}

void handle_transfer_start() {
  Serial.println("[BLE] Transfer starting");
}

void handle_transfer_complete() {
  Serial.println("[BLE] Transfer complete");
}

}  // namespace

// uint8_t buffer[256];


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

  reset_fallback_clock();

  // NimBLEDevice::setMTU(185);
  bleServer.begin();
  bleServer.onErase = handle_ble_erase;
  bleServer.onTimeSync = handle_ble_time_sync;
  bleServer.onTransferStart = handle_transfer_start;
  bleServer.onTransferComplete = handle_transfer_complete;
  Serial.println("[MAIN] BLE server initialized");

  sensors_setup(&gRing);
}

void loop() {
  sensors_loop();
  
  // const uint32_t now = millis();
  // if (now - gLastProcess >= kLoopIntervalMs) {
  //   gLastProcess = now;
  //   // process_register_buffer();
  // }
  

  // ble_service::loop();

  // if (wifi_mgr::tick()) {
  //   delay(20);
  // }
  // else {
  //   Serial.println("WiFi not connected, retrying...");
  //   delay(5000);  // Retry every 5 seconds if not connected
  if (gResetRingRequested) {
    gRing.clear();
    gResetRingRequested = false;
  }

  // reg_buffer::Sample sample{}; // initialize cycle reading struct


  // mockdata::mockReadIMU(sample.ax, sample.ay, sample.az,
  //                       sample.gx, sample.gy, sample.gz);
  // mockdata::mockReadHR(sample.hr_x10); // generating mock data
  // mockdata::mockReadTemp(sample.temp_x100);


  // const time_t now = time(nullptr);

  // if (now > 0) {
  //   sample.epoch_min = static_cast<uint32_t>(now / 60);
  // } else {
  //   const uint32_t base = gFallbackBaseMillis;
  //   const uint32_t elapsed_ms = millis() - base;
  //   sample.epoch_min = static_cast<uint32_t>(elapsed_ms / 60000UL);
  // }
  
  // Serial.printf("Sample: epoch_min=%lu ax=%d ay=%d az=%d gx=%d gy=%d gz=%d hr_x10=%u temp_x100=%d\n",
  //               static_cast<unsigned long>(sample.epoch_min),
  //               sample.ax, sample.ay, sample.az,
  //               sample.gx, sample.gy, sample.gz,
  //               sample.hr_x10, sample.temp_x100);

  // if (!gRing.push(sample)) {
  //   Serial.println("Ring buffer overrun");
  // } else {
  //   // Serial.printf("Ring buffer size: %u\n", static_cast<unsigned>(gRing.size()));
  // }


  consolidate::ConsolidatedRecord record{};
  if (consolidate::consolidate_from_ring(gRing, record)) {
    if (fs_store::append(record)) {
      Serial.println("[STORE] Consolidated record appended");
      fs_store::printData();
    } else {
      Serial.println("[STORE] Failed to append record");
    }
  }
  bleServer.update();
  delay(5);

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

  // delay(1000);
}


