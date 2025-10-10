#include <Arduino.h>
#include <string>
#include <vector>

#include "app_config.h"
#include "ble/ble_service.h"
#include "compute/consolidate.h"
#include "ringbuf/reg_buffer.h"
#include "storage/fs_store.h"
#include "wifi/wifi_mgr.h"

void setup() {
  Serial.begin(115200);
  delay(1000);  // Give serial time to initialize
  
  Serial.println();
  Serial.println("============================");
  Serial.println("ESP32 MINIMAL TEST - Version 1.0");
  Serial.println("============================");
  Serial.println("Boot successful!");
  Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Chip model: ");
  Serial.println(ESP.getChipModel());
  Serial.print("CPU frequency: ");
  Serial.println(ESP.getCpuFreqMHz());
  Serial.println("Entering main loop...");
}

void loop() {
  static uint32_t lastPrint = 0;
  uint32_t now = millis();
  
  if (now - lastPrint >= 5000) {  // Print every 5 seconds
    lastPrint = now;
    Serial.print("Alive - uptime: ");
    Serial.print(now / 1000);
    Serial.print("s, free heap: ");
    Serial.println(ESP.getFreeHeap());
  }
  
  delay(100);  // Small delay to prevent watchdog issues
}