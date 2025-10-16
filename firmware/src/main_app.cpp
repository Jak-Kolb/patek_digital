#include <Arduino.h>
#include <string>
#include <vector>

#include "app_config.h"
#include "wifi/wifi_mgr.h"
#include "ringbuf/reg_buffer.h"
#include "compute/consolidate.h"
#include "storage/fs_store.h"

// Subsystem 1
#include "i2c_bus.h"
#include "sensors.h"
#include "sub1_mux.h"

uint8_t buffer[256];

// Timers for non-blocking sampling cadences
static uint32_t t_ppg  = 0;
static uint32_t t_imu  = 0;
static uint32_t t_temp = 0;

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
  } else {
    Serial.println("[MAIN] Filesystem initialized successfully.");
  }

  // Bring up Wi-Fi (non-blocking tick used in loop)
  wifi_mgr::begin();
  delay(100);

  // Initialize the 256-byte page ring buffer
  reg_buffer::begin();

  // --- Subsystem 1 bring-up: I2C + sensors (Arduino core) ---
  i2c_setup();     // Wire.begin(21,22) + 400 kHz (see app_config.h / i2c_bus)
  sensors_init();  // light init for MAX30102, BMI270, MAX30205
  sub1_mux_begin();// start 256B page mux
  Serial.println("[SUB1] I2C+Sensors initialized.");
}

void loop() {
  const uint32_t now = millis();

  // Reusable sample struct; fields are updated on their cadence
  static Sub1Sample sample{};
  sample.ts_ms = now; // always stamp current time

  // ~50 Hz PPG (MAX30102)
  if ((now - t_ppg) >= PPG_INTERVAL_MS) {
    uint32_t ppg;
    if (read_ppg(ppg)) {
      sample.ppg_raw = ppg;
      sub1_mux_add(sample);    // write to 256B page
      // Serial.printf("PPG: %lu\n", (unsigned long)ppg); // optional debug
    }
    t_ppg = now;
  }

  // ~100 Hz IMU (BMI270 accel)
  if ((now - t_imu) >= IMU_INTERVAL_MS) {
    int16_t ax, ay, az;
    if (read_accel(ax, ay, az)) {
      sample.ax = ax; sample.ay = ay; sample.az = az;
      sub1_mux_add(sample);
      // Serial.printf("ACC: %d,%d,%d\n", ax, ay, az); // optional debug
    }
    t_imu = now;
  }

  // ~1 Hz Temp (MAX30205)
  if ((now - t_temp) >= TEMP_INTERVAL_MS) {
    float tc;
    if (read_temp(tc)) {
      sample.temp_c = tc;
      sub1_mux_add(sample);
      // Serial.printf("TEMP: %.2f C\n", tc); // optional debug
    }
    t_temp = now;
  }

  // ====== Existing Wi-Fi state machine (non-blocking) ======
  if (wifi_mgr::tick()) {
    delay(2);
  } else {
    static uint32_t lastRetry = 0;
    if (now - lastRetry >= 5000) {
      Serial.println("WiFi not connected, retrying...");
      lastRetry = now;
    }
  }

  // Optional: show ring depth once per second
  /*
  static uint32_t last_dbg = 0;
  if (now - last_dbg > 1000) {
    Serial.printf("[RB] size=%u/%u\n",
      (unsigned)reg_buffer::size(),
      (unsigned)reg_buffer::capacity());
    last_dbg = now;
  }
  */

  delay(2); // light idle keeps cadences accurate
}
