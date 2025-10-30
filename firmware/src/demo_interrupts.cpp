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
#include "app_config.h"
#include <stdint.h>
#include "esp_timer.h"

// Fallback if library doesn't define this macro
#ifndef I2C_SPEED_FAST
#define I2C_SPEED_FAST 400000
#endif

// Layout summary (matches buffer_layout.h):
// - 20 bytes/sample: [HR u16][Temp i16 F*100][Accel ax/ay/az i16 mg][Gyro gx/gy/gz i16 deci-dps][ts u32 ms]
// - 256B page: 16B header + 12 samples (12 * 20B = 240B)

// Sensors
MAX30105 particleSensor;    // MAX30102 via MAX30105 class
BMI270   imu;
Adafruit_AHTX0 aht;
static bool g_has_max30102 = false;
static bool g_has_bmi270 = false;
static bool g_has_aht20 = false;

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

// --- Timer-driven sampling (no external sensor IRQ pins) ---
static esp_timer_handle_t tmr_ppg  = nullptr; // 2 ms: service MAX30102 FIFO
static esp_timer_handle_t tmr_pack = nullptr; // 10 ms: pack IMU sample (100 Hz)
static esp_timer_handle_t tmr_status = nullptr; // 1 s: status line
static TaskHandle_t       worker_task = nullptr; // FreeRTOS worker consuming timer ticks
static portMUX_TYPE       tickMux = portMUX_INITIALIZER_UNLOCKED; // protect tick counters
static volatile uint32_t ppg_service_ticks = 0; // pending service ticks (consumed by loop)
static volatile uint32_t imu_pack_ticks    = 0; // pending pack ticks (consumed by loop)
static volatile uint32_t ppg_tick_count    = 0; // total ticks (for diagnostics)
static volatile uint32_t imu_tick_count    = 0; // total ticks (for diagnostics)
static constexpr int PPG_PERIOD_US = 2000;   // 2 ms => 500 Hz
static constexpr int IMU_PERIOD_US = 10000;  // 10 ms => 100 Hz
// Fairness and pacing limits for the worker loop
static constexpr int PPG_CHECKS_PER_PASS_MAX = 8;  // max particleSensor.check() calls per pass
static constexpr int PPG_DRAIN_PER_PASS_MAX  = 8;  // max MAX30102 FIFO samples drained per pass
static constexpr int IMU_PACKS_PER_PASS_MAX  = 10; // max IMU packs per pass
static constexpr int IMU_FALLBACK_MS         = 10; // if no ticks, still pack every ~10 ms
// Measured rates for proof
static volatile uint32_t ppg_sample_count = 0;  // consumed IR samples
static uint32_t ppg_sample_count_last = 0;
static volatile uint32_t imu_sample_count = 0;  // packed IMU samples (worker updates, status reads)
static volatile uint32_t temp_sample_count = 0; // temperature updates (~1 Hz)
// Last IMU readings for display (worker updates, status reads)
static volatile int16_t last_ax_mg = 0, last_ay_mg = 0, last_az_mg = 0;
static volatile int16_t last_gx_dps10 = 0, last_gy_dps10 = 0, last_gz_dps10 = 0;
static uint32_t imu_sample_count_last = 0;

// Store last full page averages for concise status printing
struct PageStats {
  bool valid = false;
  uint8_t seq = 0;
  uint32_t span_ms = 0; // ts[last]-ts[first]
  float hr_avg = NAN;
  float tempF_avg = NAN;
  float ax_g = 0, ay_g = 0, az_g = 0;
  float gx_dps = 0, gy_dps = 0, gz_dps = 0;
};
static PageStats g_last_page;

// Timer callbacks (ESP timer task context)
// Forward declarations used in timer callbacks
static inline int16_t to_mg_int16(float g);
static inline int16_t to_dps_x10_int16(float dps);
static inline int16_t tempF_fromC_to_i16x100(float tc);
static void page_add_sample(const SamplePacked &s);

static void IRAM_ATTR ppg_timer_cb(void*)  {
  // esp_timer callbacks run in ESP_TIMER_TASK (task context), use non-ISR critical sections
  portENTER_CRITICAL(&tickMux);
  ppg_tick_count++; ppg_service_ticks++;
  portEXIT_CRITICAL(&tickMux);
}
static void IRAM_ATTR imu_timer_cb(void*)  {
  portENTER_CRITICAL(&tickMux);
  imu_tick_count++; imu_pack_ticks++;
  portEXIT_CRITICAL(&tickMux);
}
// Forward declare conversion used in status timer
static inline float c_to_f(float tc);
// Periodic status (1 Hz): prints last page averages and measured/derived rates.
// Hz and samples/sec are equivalent; we label in Hz and also show a derived IMU rate
// from the page span as an independent cross-check of the 100 Hz target.
static void status_timer_cb(void*) {
  // Compute deltas for rates and ticks safely in this task context
  static uint32_t ppg_last = 0, imu_last = 0, temp_last = 0, ptick_last = 0, itick_last = 0;
  uint32_t ppg_hz = ppg_sample_count - ppg_last; ppg_last = ppg_sample_count;
  uint32_t imu_hz = imu_sample_count - imu_last; imu_last = imu_sample_count;
  uint32_t temp_hz = temp_sample_count - temp_last; temp_last = temp_sample_count;
  uint32_t ppg_tick_hz = ppg_tick_count - ptick_last; ptick_last = ppg_tick_count;
  uint32_t imu_tick_hz = imu_tick_count - itick_last; itick_last = imu_tick_count;
  // Snapshot pending IMU ticks for visibility
  uint32_t imu_q = 0; portENTER_CRITICAL(&tickMux); imu_q = imu_pack_ticks; portEXIT_CRITICAL(&tickMux);

  // Derive IMU rate from page span when available
  uint32_t imu_rate_from_page = 0;
  if (g_last_page.valid && g_last_page.span_ms > 0) {
    imu_rate_from_page = (uint32_t)lroundf(((kSamplesPerPage - 1) * 1000.0f) / g_last_page.span_ms);
  }

  if (g_last_page.valid) {
    Serial.printf(
      "[RB] seq=%u | avg: HR=%.0f BPM, Temp=%.2f F, A(g)=[%.3f,%.3f,%.3f], G(deg/s)=[%.1f,%.1f,%.1f] | page span=%lums | rate (Hz): PPG=%lu, IMU=%lu (derived~%lu), Temp=%lu | rb_pages=%u\n",
      (unsigned)g_last_page.seq,
      g_last_page.hr_avg,
      g_last_page.tempF_avg,
      g_last_page.ax_g, g_last_page.ay_g, g_last_page.az_g,
      g_last_page.gx_dps, g_last_page.gy_dps, g_last_page.gz_dps,
      (unsigned long)g_last_page.span_ms,
      (unsigned long)ppg_hz, (unsigned long)imu_hz, (unsigned long)imu_rate_from_page, (unsigned long)temp_hz,
      (unsigned)reg_buffer::size()
    );
  } else {
    Serial.printf(
      "[RB] waiting for first full page... | rate (Hz): PPG=%lu, IMU=%lu, Temp=%lu | rb_pages=%u\n",
      (unsigned long)ppg_hz, (unsigned long)imu_hz, (unsigned long)temp_hz,
      (unsigned)reg_buffer::size()
    );
  }
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

// Dedicated worker task: consumes timer-driven ticks and performs sensor IO/packing
static void worker_fn(void* arg) {
  (void)arg;
  for (;;) {
    bool did_work = false;
    // 1) PPG service: keep bounded per-iteration to avoid starving IMU ---
    if (g_has_max30102) {
      uint32_t sv = 0; portENTER_CRITICAL(&tickMux); sv = ppg_service_ticks; ppg_service_ticks = 0; portEXIT_CRITICAL(&tickMux);
      if (sv > PPG_CHECKS_PER_PASS_MAX) sv = PPG_CHECKS_PER_PASS_MAX; // cap how many check() we do per pass
      for (uint32_t i = 0; i < sv; ++i) particleSensor.check();
      // Drain only a small batch from the internal buffer per pass (like demo_copy one-sample behavior)
      int drained = 0;
      const int kMaxDrainPerPass = PPG_DRAIN_PER_PASS_MAX; // small, prevents long blocking
      while (particleSensor.available() && drained < kMaxDrainPerPass) {
        long raw_ir = particleSensor.getIR();
        particleSensor.nextSample();
        ppg_sample_count++;
        if (checkForBeat(raw_ir)) {
          unsigned long t = millis();
          unsigned long dt = (lastBeatMs == 0) ? 0u : (t - lastBeatMs);
          lastBeatMs = t;
          if (dt > 0) {
            float bpm = 60.0f / (dt / 1000.0f);
            if (bpm >= 30.0f && bpm <= 220.0f) last_bpm = bpm;
          }
        }
        drained++;
      }
      if (sv > 0) did_work = true;
      if (drained > 0) did_work = true;
    }

    // 2) Temperature update at ~1 Hz regardless of IMU queue state ---
    if (g_has_aht20) {
      static unsigned long last_t_ms = 0; unsigned long now_t = millis();
      if (now_t - last_t_ms >= TEMP_PERIOD_MS) {
        sensors_event_t h, t; aht.getEvent(&h, &t);
        if (isfinite(t.temperature)) { last_temp_c = t.temperature; temp_sample_count++; }
        last_t_ms = now_t;
      }
    }

    // Helper to build and push one packed sample using latest IMU and cached HR/Temp
    auto do_one_pack = [&]() {
      float ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
      if (g_has_bmi270 && imu.getSensorData() == BMI2_OK) {
        ax = imu.data.accelX; ay = imu.data.accelY; az = imu.data.accelZ;
        gx = imu.data.gyroX;  gy = imu.data.gyroY;  gz = imu.data.gyroZ;
      }
      // Update last IMU snapshot for display
      last_ax_mg = to_mg_int16(ax);
      last_ay_mg = to_mg_int16(ay);
      last_az_mg = to_mg_int16(az);
      last_gx_dps10 = to_dps_x10_int16(gx);
      last_gy_dps10 = to_dps_x10_int16(gy);
      last_gz_dps10 = to_dps_x10_int16(gz);

      SamplePacked s{};
      s.hr = (uint16_t)lroundf(last_bpm);
      s.temp_raw = isfinite(last_temp_c) ? tempF_fromC_to_i16x100(last_temp_c) : 0;
      s.ax = to_mg_int16(ax);
      s.ay = to_mg_int16(ay);
      s.az = to_mg_int16(az);
      s.gx = to_dps_x10_int16(gx);
      s.gy = to_dps_x10_int16(gy);
      s.gz = to_dps_x10_int16(gz);
      s.ts_ms = millis();
      page_add_sample(s);
      imu_sample_count++;
    };

    // 3) IMU packing: consume at most a few queued ticks per pass, always make forward progress ---
    int packs_done = 0;
    for (int i = 0; i < IMU_PACKS_PER_PASS_MAX; ++i) { // handle up to 10 IMU packs per pass
      bool have_tick = false;
      portENTER_CRITICAL(&tickMux);
      if (imu_pack_ticks > 0) { imu_pack_ticks--; have_tick = true; }
      portEXIT_CRITICAL(&tickMux);
      if (have_tick) { do_one_pack(); packs_done++; }
      else break;
    }
    // Fallback: if no queued ticks, still do ~100 Hz pacing
    static unsigned long last_pack_ms = 0; unsigned long now2 = millis();
  if (packs_done == 0 && (now2 - last_pack_ms) >= IMU_FALLBACK_MS) { do_one_pack(); last_pack_ms = now2; }
    if (packs_done > 0) did_work = true;

    // Yield to other tasks; 1 ms is plenty
    if (!did_work) {
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      taskYIELD();
    }
  }
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
    const SamplePacked* samples = reinterpret_cast<const SamplePacked*>(page + kPageHeaderBytes);
    // Compute page averages and span
    double sum_hr = 0; uint32_t cnt_hr = 0;
    double sum_tempF = 0; uint32_t cnt_temp = 0;
    double sum_ax = 0, sum_ay = 0, sum_az = 0;
    double sum_gx = 0, sum_gy = 0, sum_gz = 0;
    uint32_t ts0 = samples[0].ts_ms;
    uint32_t ts1 = samples[kSamplesPerPage - 1].ts_ms;
    for (size_t i = 0; i < kSamplesPerPage; ++i) {
      const SamplePacked& sp = samples[i];
      if (sp.hr > 0) { sum_hr += sp.hr; cnt_hr++; }
      if (sp.temp_raw != 0) { sum_tempF += ((double)sp.temp_raw) / 100.0; cnt_temp++; }
      sum_ax += ((double)sp.ax) / 1000.0; // mg -> g
      sum_ay += ((double)sp.ay) / 1000.0;
      sum_az += ((double)sp.az) / 1000.0;
      sum_gx += ((double)sp.gx) / 10.0;   // deci-dps -> dps
      sum_gy += ((double)sp.gy) / 10.0;
      sum_gz += ((double)sp.gz) / 10.0;
    }
    g_last_page.valid = true;
    g_last_page.seq = ph->seq;
    g_last_page.span_ms = (ts1 >= ts0) ? (ts1 - ts0) : 0;
    g_last_page.hr_avg = cnt_hr ? (float)(sum_hr / cnt_hr) : NAN;
    g_last_page.tempF_avg = cnt_temp ? (float)(sum_tempF / cnt_temp) : NAN;
    g_last_page.ax_g = (float)(sum_ax / kSamplesPerPage);
    g_last_page.ay_g = (float)(sum_ay / kSamplesPerPage);
    g_last_page.az_g = (float)(sum_az / kSamplesPerPage);
    g_last_page.gx_dps = (float)(sum_gx / kSamplesPerPage);
    g_last_page.gy_dps = (float)(sum_gy / kSamplesPerPage);
    g_last_page.gz_dps = (float)(sum_gz / kSamplesPerPage);

    reg_buffer::push_256(page);
    page_begin();
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  // Keep I2C transactions from hanging indefinitely
  Wire.setTimeOut(50);
  reg_buffer::begin();
  page_begin();
  // Force first status line quickly
  last_debug_ms = millis() - 1000;

  // Timers and worker will be started after sensors initialize

  // Setup banner so you immediately see configuration on monitor
  Serial.println();
  Serial.println("===== demo_interrupts: timer-driven sampling =====");
  Serial.printf("I2C: SDA=%d SCL=%d clk=%lu Hz (target up to 400 kHz)\n", I2C_SDA_PIN, I2C_SCL_PIN, (unsigned long)I2C_CLOCK_HZ);
  Serial.println("Timers:");
  Serial.println("  - PPG service: 2 ms (MAX30102 FIFO)");
  Serial.println("  - IMU pack:    10 ms (100 Hz)");
  Serial.println("  - Temp read:   1000 ms (1 Hz)");
  Serial.printf("Ring buffer: page=%u B, capacity=%u pages\n", (unsigned)REG_BUFFER_PAGE_BYTES, (unsigned)reg_buffer::capacity());
  Serial.println("Sensors: MAX30102 + BMI270 + AHT20");
  Serial.println("==================================================");

  // Quick I2C presence probe to avoid blocking begin() calls
  auto i2c_ping = [](uint8_t addr) {
    Wire.beginTransmission(addr);
    uint8_t rc = Wire.endTransmission();
    return rc == 0;
  };
  g_has_max30102 = i2c_ping(0x57);
  g_has_bmi270   = i2c_ping(0x68);
  g_has_aht20    = i2c_ping(0x38);
  Serial.printf("I2C probe: MAX30102(0x57)=%s, BMI270(0x68)=%s, AHT20(0x38)=%s\n",
                g_has_max30102?"yes":"no",
                g_has_bmi270?"yes":"no",
                g_has_aht20?"yes":"no");

  // MAX30102
  if (g_has_max30102) {
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
      Serial.println("MAX30102 begin() failed (present on bus)");
      g_has_max30102 = false;
    } else {
      // Stable config (matches demo_copy):
      particleSensor.setup();
      // Ensure PPG acquisition rate >= 50 Hz (use 100 samples/sec)
      particleSensor.setSampleRate(100);
      // IR LED on, small Red helps library modes; Green off
      particleSensor.setPulseAmplitudeIR(0x60);
      particleSensor.setPulseAmplitudeRed(0x0A);
      particleSensor.setPulseAmplitudeGreen(0);
      // Ensure no extra averaging (we want full 100 sps)
      particleSensor.setFIFOAverage(1);
      particleSensor.clearFIFO();
    }
  } else {
    Serial.println("MAX30102 absent (addr 0x57)");
  }

  // BMI270
  if (g_has_bmi270) {
    if (imu.beginI2C(0x68, Wire) == BMI2_OK) {
      imu.setAccelODR(BMI2_ACC_ODR_100HZ);
      imu.setGyroODR(BMI2_GYR_ODR_100HZ);
    } else {
      Serial.println("BMI270 begin() failed (present on bus)");
      g_has_bmi270 = false;
    }
  } else {
    Serial.println("BMI270 absent (addr 0x68)");
  }

  // AHT20
  if (g_has_aht20) {
    if (!aht.begin()) {
      Serial.println("AHT20 begin() failed (present on bus)");
      g_has_aht20 = false;
    }
  } else {
    Serial.println("AHT20 absent (addr 0x38)");
  }

  // ESP32 esp_timer periodic callbacks for fixed-rate scheduling (start AFTER sensors init)
  {
    esp_timer_create_args_t args{ };
    args.callback = &ppg_timer_cb;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "ppg";
    ESP_ERROR_CHECK(esp_timer_create(&args, &tmr_ppg));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tmr_ppg, PPG_PERIOD_US));
  }
  {
    esp_timer_create_args_t args{ };
    args.callback = &imu_timer_cb;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "imu";
    ESP_ERROR_CHECK(esp_timer_create(&args, &tmr_pack));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tmr_pack, IMU_PERIOD_US));
  }
  {
    esp_timer_create_args_t args{ };
    args.callback = &status_timer_cb;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "status";
    ESP_ERROR_CHECK(esp_timer_create(&args, &tmr_status));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tmr_status, 1000000)); // 1 second
  }

  // Create the worker task (core 1, medium priority) after timers exist
  xTaskCreatePinnedToCore(worker_fn, "sampler", 4096, nullptr, 2, &worker_task, 1);
}

void loop() {
  unsigned long now = millis();

  // MAX30102 serviced by worker task from timer ticks

  // AHT20 updated in worker task ~1 Hz

  // BMI270 packed in worker task from timer ticks

  // Status lines are printed by status_timer_cb() every second
  // Optional tiny heartbeat to prove loop is alive while waiting for first 1s print
  #if 0
  static unsigned long hb_ms = 0; if (millis() - hb_ms >= 100) { Serial.print('.'); hb_ms = millis(); }
  #endif
}
