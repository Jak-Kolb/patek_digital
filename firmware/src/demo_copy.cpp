// demo_copy.cpp
// - Reads: MAX30102 (HR via beat detection), BMI270 (accel/gyro), AHT20 (temp)
// - Packs 12 samples per 256B page: 16B header + 12 x 20B samples (see buffer_layout.h)
// - Sample fields per 20B: HR (u16 BPM), Temp (i16 F*100), Accel (mg), Gyro (deci-dps), Timestamp (ms)

#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_AHTX0.h>
#include "SparkFun_BMI270_Arduino_Library.h"
#include "ringbuf/reg_buffer.h"
#include "buffer_layout.h"

// Layout summary (matches buffer_layout.h):
// - 20 bytes/sample: [HR u16][Temp i16 F*100][Accel ax/ay/az i16 mg][Gyro gx/gy/gz i16 deci-dps][ts u32 ms]
// - 256B page: 16B header + 12 samples (12 * 20B = 240B)

// Sensors
MAX30105 particleSensor;    // MAX30102 via MAX30105 class
BMI270   imu;
Adafruit_AHTX0 aht;

// Sampling & pack cadence (spec requirement)
// - PPG acquisition: >= 50 Hz (we configure MAX30102 to 100 sps)
// - IMU data:       >= 100 Hz (we pack samples every 10 ms)
// - Temperature:     ~ 1 Hz
static constexpr unsigned long SAMPLE_PERIOD_MS = 10; // 100 Hz: one packed sample every 10 ms

// Current measurements cached between samples
static float last_bpm = 0.0f;         // from MAX30102 beat detections
static float last_temp_c = NAN;       // raw Celsius from AHT20 (~1 Hz updates)
static unsigned long last_temp_ms = 0;
static constexpr unsigned long TEMP_PERIOD_MS = 1000; // 1 Hz: one packed sample every 1000 ms

// Beat timing state
static unsigned long lastBeatMs = 0;

// Page header (16 bytes)
struct PageHeader {
  char magic[4];    // 'H','P','K','1'
  uint8_t seq;      // incrementing sequence
  uint8_t reserved[11];
};

static uint8_t page[256];
static uint8_t sample_index = 0; // 0..11
static uint8_t page_seq = 0;
static unsigned long last_debug_ms = 0; // rate-limit serial debug
static unsigned long last_page_print_ms = 0; // throttle page summaries

// Audience-friendly page summary when a 256B page is pushed
static void print_page_summary(const uint8_t* buf) {
  const PageHeader* ph = reinterpret_cast<const PageHeader*>(buf);
  const SamplePacked* samples = reinterpret_cast<const SamplePacked*>(buf + kPageHeaderBytes);
  // Safety: expect exactly kSamplesPerPage samples
  uint32_t ts0 = samples[0].ts_ms;
  uint32_t ts1 = samples[kSamplesPerPage - 1].ts_ms;
  uint32_t span = (ts1 >= ts0) ? (ts1 - ts0) : 0;

  // Compute quick averages (ignore zero values)
  uint32_t hr_sum = 0, hr_cnt = 0;
  for (size_t i = 0; i < kSamplesPerPage; ++i) {
    uint16_t hr = samples[i].hr;
    if (hr > 0) { hr_sum += hr; hr_cnt++; }
  }
  const char* hr_str = "n/a"; char hr_buf[8];
  if (hr_cnt) { snprintf(hr_buf, sizeof(hr_buf), "%lu", (unsigned long)(hr_sum / hr_cnt)); hr_str = hr_buf; }

  Serial.printf(
    "[RB] Page seq=%u | samples=%u | time span=%lums | HR avg=%s BPM | pages buffered=%u\n",
    (unsigned)ph->seq,
    (unsigned)kSamplesPerPage,
    (unsigned long)span,
    hr_str,
    (unsigned)reg_buffer::size()
  );
}

// Helpers to scale sensor floats into int16 payload fields
static inline int16_t to_mg_int16(float g) {
  // Convert g to milli-g and cut off range
  long mg = lroundf(g * 1000.0f);
  if (mg > 32767) mg = 32767; else if (mg < -32768) mg = -32768;
  return (int16_t)mg;
}

static inline int16_t to_dps_x10_int16(float dps) {
  // Convert deg/s to deci-deg/s
  long v = lroundf(dps * 10.0f);
  if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
  return (int16_t)v;
}

// Convert Celsius to Fahrenheit*100 scaled int16 for packing
static inline float c_to_f(float tc) { return tc * 9.0f / 5.0f + 32.0f; }
static inline int16_t tempF_fromC_to_i16x100(float tc) {
  float tf = c_to_f(tc);
  long f100 = lroundf(tf * 100.0f);
  if (f100 > 32767) f100 = 32767; else if (f100 < -32768) f100 = -32768;
  return (int16_t)f100;
}

static void page_begin() {
  PageHeader hdr{};
  hdr.magic[0] = 'H'; hdr.magic[1] = 'P'; hdr.magic[2] = 'K'; hdr.magic[3] = '1';
  hdr.seq = page_seq++;
  memcpy(page, &hdr, sizeof(hdr));
  sample_index = 0;
}

static void page_add_sample(const SamplePacked &s) {
  // 16B header + N*20B samples
  const size_t offset = kPageHeaderBytes + (size_t)sample_index * sizeof(SamplePacked);
  memcpy(page + offset, &s, sizeof(SamplePacked));
  sample_index++;
  if (sample_index >= kSamplesPerPage) {
    const PageHeader* ph = reinterpret_cast<const PageHeader*>(page);
    uint8_t pushed_seq = ph->seq;
    reg_buffer::push_256(page);
    // Throttle to avoid spamming (max ~2 summaries/second)
    unsigned long now = millis();
    if (now - last_page_print_ms >= 500) {
      print_page_summary(page);
      last_page_print_ms = now;
    }
    page_begin();
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  reg_buffer::begin();
  page_begin();

  // MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
  } else {
    particleSensor.setup();
    // Ensure PPG acquisition rate >= 50 Hz (use 100 samples/sec)
    particleSensor.setSampleRate(100);
    // Moderate IR LED amplitude for HR detection; Green off to save power
    particleSensor.setPulseAmplitudeIR(0x30);
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeGreen(0);
  }

  // BMI270
  if (imu.beginI2C(0x68, Wire) == BMI2_OK) {
    imu.setAccelODR(BMI2_ACC_ODR_100HZ);
    imu.setGyroODR(BMI2_GYR_ODR_100HZ);
  } else {
    Serial.println("BMI270 not found");
  }

  // AHT20
  if (!aht.begin()) {
    Serial.println("AHT20 not found");
  }
}

void loop() {
  unsigned long now = millis();

  // --- MAX30102: fetch raw IR and update last_bpm on beats ---
  // Use SparkFun checkForBeat on raw IR. Keeps last_bpm updated when beats are detected.
  while (particleSensor.available() == false) particleSensor.check();
  long raw_ir = particleSensor.getIR();
  particleSensor.nextSample();
  if (checkForBeat(raw_ir)) {
    unsigned long delta = (lastBeatMs == 0) ? 0u : (now - lastBeatMs);
    lastBeatMs = now;
    if (delta > 0) {
      float bpm = 60.0f / (delta / 1000.0f);
      if (bpm >= 30.0f && bpm <= 220.0f) last_bpm = bpm;
    }
  }

  // --- AHT20: update temperature at ~1 Hz ---
  // Keep last_temp_c in Celsius; convert to Fahrenheit only when packing/printing
  if (now - last_temp_ms >= TEMP_PERIOD_MS) {
    sensors_event_t h, t;
    aht.getEvent(&h, &t);
    if (isfinite(t.temperature)) last_temp_c = t.temperature;
    last_temp_ms = now;
  }

  // --- BMI270: read latest accel/gyro and pack sample at 100 Hz ---
  static unsigned long last_sample = 0;
  if (now - last_sample >= SAMPLE_PERIOD_MS) {
    float ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
    if (imu.getSensorData() == BMI2_OK) {
      ax = imu.data.accelX; ay = imu.data.accelY; az = imu.data.accelZ;
      gx = imu.data.gyroX;  gy = imu.data.gyroY;  gz = imu.data.gyroZ;
    }

  // Build packed sample (20B) in the exact order defined in buffer_layout.h
  SamplePacked s{};
  s.hr = (uint16_t)lroundf(last_bpm);
  // Store temperature as Fahrenheit * 100 (int16)
  s.temp_raw = isfinite(last_temp_c) ? tempF_fromC_to_i16x100(last_temp_c) : 0;
  s.ax = to_mg_int16(ax);
  s.ay = to_mg_int16(ay);
  s.az = to_mg_int16(az);
  s.gx = to_dps_x10_int16(gx);
  s.gy = to_dps_x10_int16(gy);
  s.gz = to_dps_x10_int16(gz);
  s.ts_ms = now;

    page_add_sample(s);

    // 1 Hz debug print of current readings and buffer depth
    if (now - last_debug_ms >= 1000) {
      const char* temp_str = "n/a";
      char temp_buf[16];
      if (isfinite(last_temp_c)) {
        float tf = c_to_f(last_temp_c);
        snprintf(temp_buf, sizeof(temp_buf), "%.2f", tf);
        temp_str = temp_buf;
      }
      // Convert to human-friendly units for the demo
      float ax_g = (float)s.ax / 1000.0f;
      float ay_g = (float)s.ay / 1000.0f;
      float az_g = (float)s.az / 1000.0f;
      float gx_dps = (float)s.gx / 10.0f;
      float gy_dps = (float)s.gy / 10.0f;
      float gz_dps = (float)s.gz / 10.0f;
      Serial.printf(
        "HR=%u BPM | Temp=%s F | Accel (g)=[%.3f, %.3f, %.3f] | Gyro (deg/s)=[%.1f, %.1f, %.1f] | Sample ts=%lu ms | Page slot=%u/%u | Pages buffered=%u\n",
        (unsigned)s.hr,
        temp_str,
        ax_g, ay_g, az_g,
        gx_dps, gy_dps, gz_dps,
        (unsigned long)s.ts_ms,
        (unsigned)sample_index, (unsigned)kSamplesPerPage,
        (unsigned)reg_buffer::size()
      );
      last_debug_ms = now;
    }
    last_sample = now;
  }
}
