// Minimal BMI + PPG demo: print only HR (last), 10s AVG HR, and BMI270 accel/gyro at <= 5 Hz

#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "SparkFun_BMI270_Arduino_Library.h"

// MAX30102 (via MAX30105 class)
MAX30105 particleSensor;

// BMI270
BMI270 imu;

// 10-second BPM averaging buffer (store detected beat BPM values with timestamps)
struct BPMEntry { unsigned long ts; float bpm; };
static const size_t BPM_WINDOW_MAX = 64;
static BPMEntry bpm_window[BPM_WINDOW_MAX];
static size_t bpm_head = 0; // next write index
static size_t bpm_count = 0;

static void push_bpm(unsigned long ts, float bpm) {
  bpm_window[bpm_head] = { ts, bpm };
  bpm_head = (bpm_head + 1) % BPM_WINDOW_MAX;
  if (bpm_count < BPM_WINDOW_MAX) bpm_count++;
}

static float avg_bpm_last_10s(unsigned long now_ms) {
  if (bpm_count == 0) return 0.0f;
  const unsigned long cutoff = (now_ms > 10000u) ? (now_ms - 10000u) : 0u;
  float sum = 0.0f; int n = 0;
  for (size_t i = 0; i < bpm_count; ++i) {
    size_t idx = (bpm_head + BPM_WINDOW_MAX - 1 - i) % BPM_WINDOW_MAX; // newest -> oldest
    BPMEntry &e = bpm_window[idx];
    if (e.ts >= cutoff) { sum += e.bpm; ++n; }
    else break;
  }
  return (n > 0) ? (sum / n) : 0.0f;
}

// IMU state and sampling
static float ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
static constexpr unsigned long IMU_SAMPLE_MS = 100; // 10 Hz internal read
static unsigned long lastIMU = 0;

// Print rate cap (<= 5 Hz)
static constexpr unsigned long PRINT_INTERVAL_MS = 200; // 5 Hz
static unsigned long lastPrint = 0;

// Heartbeat state
static unsigned long lastBeatMs = 0;
static float last_bpm = 0.0f;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    while (1) delay(100);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  if (imu.beginI2C(0x68, Wire) == BMI2_OK) {
    imu.setAccelODR(BMI2_ACC_ODR_100HZ);
    imu.setGyroODR(BMI2_GYR_ODR_100HZ);
  }
}

void loop() {
  // Ensure a PPG sample is ready (library FIFO)
  while (particleSensor.available() == false) particleSensor.check();
  long raw_ir = particleSensor.getIR();
  particleSensor.nextSample();

  unsigned long now = millis();

  // Read IMU at ~10 Hz
  if (now - lastIMU >= IMU_SAMPLE_MS) {
    if (imu.getSensorData() == BMI2_OK) {
      ax = imu.data.accelX; ay = imu.data.accelY; az = imu.data.accelZ;
      gx = imu.data.gyroX;  gy = imu.data.gyroY;  gz = imu.data.gyroZ;
    }
    lastIMU = now;
  }

  // Beat detect using raw IR
  if (checkForBeat(raw_ir)) {
    unsigned long ts = now;
    unsigned long delta = (lastBeatMs == 0) ? 0u : (ts - lastBeatMs);
    lastBeatMs = ts;
    if (delta > 0) {
      float bpm = 60.0f / (delta / 1000.0f);
      if (bpm >= 30.0f && bpm <= 220.0f) {
        last_bpm = bpm;
        push_bpm(ts, bpm);
      }
    }
  }

  // Print no more than 5 times per second
  if (now - lastPrint >= PRINT_INTERVAL_MS) {
    float avg10 = avg_bpm_last_10s(now);
    Serial.printf("BPM=%.1f AVG10=%.1f ACC=%.3f,%.3f,%.3f GYRO=%.2f,%.2f,%.2f\n",
                  last_bpm, avg10, ax, ay, az, gx, gy, gz);
    lastPrint = now;
  }
}