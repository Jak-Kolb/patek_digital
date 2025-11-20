// Health Pack Demo
// What this does (plain English):
// - Reads MAX30102 for heart beats (we detect beats and keep the most recent BPM).
// - Reads BMI270 accel/gyro at 100 Hz (motion sensing).
// - Reads AHT20 temperature ~1 Hz and converts to Fahrenheit.
// - Every read is packed into a fixed 20-byte record; we store 12 of those into a 256-byte page and push it to our ring buffer.
//
// How timing works (super important for your demo):
// - PPG (MAX30102): Sensor is configured for 100 samples/sec. If its INT pin is wired, we run interrupt-driven FIFO service
//   (we set a flag in the ISR and then drain the FIFO in loop()). If not wired, we fall back to polling.
// - IMU (BMI270): If its data-ready INT pin is wired, we sample on each interrupt; if not, we wake up every 10 ms (100 Hz) to sample.
// - Temp (AHT20): Read once per second.
// - We also print measured rates once per second (counts) so you can show actual Hz in the demo.

#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_AHTX0.h>
#include <MAX30205.h>
#include "SparkFun_BMI270_Arduino_Library.h"
#include "ringbuf/reg_buffer.h"
#include "buffer_layout.h"
#include "app_config.h"

// Layout summary (matches buffer_layout.h):
// - 20 bytes/sample: [HR u16][Temp i16 F*100][Accel ax/ay/az i16 mg][Gyro gx/gy/gz i16 deci-dps][ts u32 ms]
// - 256B page: 16B header + 12 samples (12 * 20B = 240B)

// Sensors
MAX30105 particleSensor;    // MAX30102 via MAX30105 class
BMI270   imu;
#if USE_AHT20
Adafruit_AHTX0 aht;
#endif
#if USE_MAX30205
MAX30205 max30205;
#endif

// Sampling & pack cadence (spec requirement)
// - PPG acquisition: >= 50 Hz (we configure MAX30102 to 100 sps)
// - IMU data:       >= 100 Hz (we pack samples every 10 ms)
// - Temperature:     ~ 1 Hz
static constexpr unsigned long SAMPLE_PERIOD_MS = 10; // 100 Hz: one packed sample every 10 ms (fallback)

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

// SPEC: Register Buffer Size — 256 bytes per page (temporary on-chip storage)
// We assemble a 16-byte header + 12 samples x 20 bytes = 256 bytes exactly,
// then push pages into the ring buffer for efficient, low-interrupt transfers.
static uint8_t page[256];
static uint8_t sample_index = 0; // 0..11
static uint8_t page_seq = 0;
static unsigned long last_debug_ms = 0; // rate-limit serial debug
static volatile bool ppg_irq_flag = false;
static volatile bool imu_irq_flag = false;
// Live counters so we can prove sampling rate during the demo
static volatile uint32_t ppg_irq_count = 0;  // how many PPG interrupts we saw
static uint32_t ppg_irq_count_last = 0;
static uint32_t ppg_sample_count = 0;       // how many IR samples we consumed via nextSample()
static uint32_t ppg_sample_count_last = 0;
static uint32_t imu_sample_count = 0;       // how many IMU samples we packed
static uint32_t imu_sample_count_last = 0;
// Debug/watchdog helpers
static uint32_t tick_ppg_accum = 0;
static uint32_t tick_imu_accum = 0;
static int16_t last_ax_mg = 0, last_ay_mg = 0, last_az_mg = 0;
static int16_t last_gx_d10 = 0, last_gy_d10 = 0, last_gz_d10 = 0;
static uint32_t last_ts_ms_print = 0;

// ESP32 hardware timers to service sensors when sensor-driven IRQs aren't available
static hw_timer_t* tmr_ppg = nullptr;  // services MAX30102 FIFO at ~2 ms
static hw_timer_t* tmr_pack = nullptr; // drives IMU read/pack at 10 ms (100 Hz)
static volatile uint32_t ppg_service_ticks = 0; // 2 ms service ticks
static volatile uint32_t imu_pack_ticks = 0;    // 10 ms pack ticks

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
    static unsigned long last_rb_log_ms = 0;
    unsigned long now_rb = millis();
    if (now_rb - last_rb_log_ms >= 1000) {
      Serial.printf("[RB] Pushed page seq=%u depth=%u\n", pushed_seq, (unsigned)reg_buffer::size());
      last_rb_log_ms = now_rb;
    }
    page_begin();
  }
}

// Print a one-shot summary of key specs for demo/screenshot
static void print_spec_banner() {
  Serial.println();
  Serial.println("================= Health Pack Spec Summary =================");
  // I2C interface spec
  Serial.printf("I2C: SDA=%d  SCL=%d  freq=%lu Hz\n", I2C_SDA_PIN, I2C_SCL_PIN, (unsigned long)I2C_CLOCK_HZ);
  // Sampling targets
  Serial.println("Sampling targets:");
  Serial.println("  - PPG (MAX30102): 100 sps (spec ≥ 50 Hz)");
  Serial.println("  - IMU (BMI270):   100 Hz   (spec ≥ 100 Hz)");
  Serial.println("  - Temp (AHT20):   ~1 Hz");
  // Interrupts
  Serial.printf("Interrupt pins: MAX30102_INT_PIN=%d (%s), BMI270_INT_PIN=%d (%s)\n",
                MAX30102_INT_PIN, (MAX30102_INT_PIN >= 0 ? "enabled" : "disabled"),
                BMI270_INT_PIN,   (BMI270_INT_PIN   >= 0 ? "enabled" : "disabled"));
  // Ring buffer/page layout
  Serial.printf("Ring buffer page: %u bytes  header=%u  sample=%u bytes  samples/page=%u\n",
                (unsigned)REG_BUFFER_PAGE_BYTES,
                (unsigned)kPageHeaderBytes,
                (unsigned)kSampleBytes,
                (unsigned)kSamplesPerPage);
  Serial.println("Sample layout (20B): [HR u16][Temp i16 F*100][Accel ax/ay/az i16 mg][Gyro gx/gy/gz i16 deci-dps][ts u32 ms]");
  // Derived pacing note
  Serial.printf("At IMU 100 Hz and %u samples/page, we push a 256B page about every %.0f ms.\n",
                (unsigned)kSamplesPerPage, (1000.0f / 100.0f) * (float)kSamplesPerPage);
  Serial.println("============================================================");
  Serial.println();
}

// INTERRUPTS: as short as possible — just set a flag and (optionally) count events
static void IRAM_ATTR on_max30102_int() { ppg_irq_flag = true; ppg_irq_count++; }
static void IRAM_ATTR on_bmi270_int()  { imu_irq_flag = true; }
static void IRAM_ATTR on_timer_ppg()   { ppg_service_ticks++; }
static void IRAM_ATTR on_timer_pack()  { imu_pack_ticks++; }

void setup() {
  Serial.begin(115200);
  // SPEC: Sensor Interface Compatibility — I2C at up to 400 kHz, using configured SDA/SCL pins
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ);
  reg_buffer::begin();
  page_begin();

  // Print banner first so the demo shows configured specs clearly
  print_spec_banner();

  // Hardware timers (fallback service when no sensor IRQs are wired)
  // Set base clock to 1 MHz (80 MHz / 80 prescaler) -> periods use microseconds
  tmr_ppg = timerBegin(0, 80, true);   // Timer 0, prescaler 80, count up
  timerAttachInterrupt(tmr_ppg, &on_timer_ppg, true);
  timerAlarmWrite(tmr_ppg, 2000, true); // 2,000 us -> 2 ms service cadence for PPG FIFO
  timerAlarmEnable(tmr_ppg);

  tmr_pack = timerBegin(1, 80, true);  // Timer 1
  timerAttachInterrupt(tmr_pack, &on_timer_pack, true);
  timerAlarmWrite(tmr_pack, 10000, true); // 10,000 us -> 10 ms (100 Hz) pack cadence
  timerAlarmEnable(tmr_pack);

  // SPEC: Sampling Rate — PPG >= 50 Hz (we set 100 sps here)
  // MAX30102 (PPG) — 100 sps target
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
  } else {
    particleSensor.setup();
    // Ensure PPG acquisition rate >= 50 Hz (use 100 samples/sec)
    #ifdef MAX30105_SAMPLERATE_100
    particleSensor.setSampleRate(MAX30105_SAMPLERATE_100);
    #else
    particleSensor.setSampleRate(100);
    #endif
    // Prefer explicit pulse width / ADC range if macros are available
    #ifdef MAX30105_PULSEWIDTH_411
    particleSensor.setPulseWidth(MAX30105_PULSEWIDTH_411);
    #else
    particleSensor.setPulseWidth(0x03); // 411us
    #endif
    #ifdef MAX30105_ADCRANGE_16384
    particleSensor.setADCRange(MAX30105_ADCRANGE_16384);
    #else
    particleSensor.setADCRange(0x60); // 16384nA
    #endif
    // Moderate IR LED amplitude for HR detection; Green off to save power
    particleSensor.setPulseAmplitudeIR(0x30);
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeGreen(0);
    // Configure INT pin if available (active low). MAX30102 INT asserts when new FIFO data is ready.
    if (MAX30102_INT_PIN >= 0) {
      pinMode(MAX30102_INT_PIN, INPUT_PULLUP);
      attachInterrupt(digitalPinToInterrupt(MAX30102_INT_PIN), on_max30102_int, FALLING);
      Serial.printf("MAX30102 INT enabled on pin %d\n", MAX30102_INT_PIN);
    }
  }

  // SPEC: Sampling Rate — IMU >= 100 Hz (we set 100 Hz here)
  // BMI270 (IMU) — 100 Hz target
  if (imu.beginI2C(0x68, Wire) == BMI2_OK) {
    imu.setAccelODR(BMI2_ACC_ODR_100HZ);
    imu.setGyroODR(BMI2_GYR_ODR_100HZ);
    // Configure BMI270 data ready interrupt if a pin is wired; library config may need enabling data ready route
    if (BMI270_INT_PIN >= 0) {
      pinMode(BMI270_INT_PIN, INPUT_PULLUP);
      attachInterrupt(digitalPinToInterrupt(BMI270_INT_PIN), on_bmi270_int, RISING);
      Serial.printf("BMI270 INT enabled on pin %d\n", BMI270_INT_PIN);
    }
  } else {
    Serial.println("BMI270 not found");
  }

  // Temperature sensor init
  #if USE_AHT20
    if (!aht.begin()) {
      Serial.println("AHT20 not found");
    }
  #endif
  #if USE_MAX30205
    if (!max30205.begin(I2C_ADDR_MAX30205)) {
      Serial.println("MAX30205 not found");
    }
  #endif
}

void loop() {
  unsigned long now = millis();

  // --- MAX30102: fetch raw IR and update last_bpm on beats ---
  // Service via IRQ if wired, else according to 2 ms timer ticks; always drain FIFO fully.
  uint32_t ppg_services = 0;
  if (MAX30102_INT_PIN >= 0) {
    if (ppg_irq_flag) { ppg_irq_flag = false; ppg_services = 1; }
  } else {
    noInterrupts();
    ppg_services = ppg_service_ticks;
    ppg_service_ticks = 0;
    interrupts();
    if (ppg_services > 4) ppg_services = 4; // bound work per loop
  }
  for (uint32_t i = 0; i < ppg_services; ++i) {
    particleSensor.check();
  }
  // Drain any accumulated PPG samples and feed beat detector
  while (particleSensor.available()) {
    long raw_ir = particleSensor.getIR();
    particleSensor.nextSample();
    ppg_sample_count++;
    if (checkForBeat(raw_ir)) {
      unsigned long t_now = millis();
      unsigned long delta = (lastBeatMs == 0) ? 0u : (t_now - lastBeatMs);
      lastBeatMs = t_now;
      if (delta > 0) {
        float bpm = 60.0f / (delta / 1000.0f);
        if (bpm >= 30.0f && bpm <= 220.0f) last_bpm = bpm;
      }
    }
  }

  // SPEC: Sampling Rate — Temperature ~1 Hz
  // --- AHT20: update temperature at ~1 Hz ---
  // Keep last_temp_c in Celsius; convert to Fahrenheit only when packing/printing
  if (now - last_temp_ms >= TEMP_PERIOD_MS) {
    #if USE_AHT20
      sensors_event_t h, t;
      aht.getEvent(&h, &t);
      if (isfinite(t.temperature)) last_temp_c = t.temperature;
    #endif
    #if USE_MAX30205
      float tc = max30205.read();
      if (isfinite(tc)) last_temp_c = tc;
    #endif
    last_temp_ms = now;
  }

  // --- BMI270: read latest accel/gyro and pack sample at 100 Hz ---
  // Use sensor IRQ if available; otherwise consume pending 10 ms timer ticks (up to 5 per loop)
  uint32_t imu_packs = 0;
  if (BMI270_INT_PIN >= 0) {
    if (imu_irq_flag) { imu_irq_flag = false; imu_packs = 1; }
  } else {
    noInterrupts();
    imu_packs = imu_pack_ticks;
    imu_pack_ticks = 0;
    interrupts();
    if (imu_packs > 5) imu_packs = 5;
  }

  SamplePacked s_last{}; // keep last for printing
  for (uint32_t k = 0; k < imu_packs; ++k) {
    float ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
    if (imu.getSensorData() == BMI2_OK) {
      ax = imu.data.accelX; ay = imu.data.accelY; az = imu.data.accelZ;
      gx = imu.data.gyroX;  gy = imu.data.gyroY;  gz = imu.data.gyroZ;
    }

    // Build packed sample (20B) in the exact order defined in buffer_layout.h
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
    s_last = s;
    // Update caches for printing even if no future packs happen
    last_ax_mg = s.ax; last_ay_mg = s.ay; last_az_mg = s.az;
    last_gx_d10 = s.gx; last_gy_d10 = s.gy; last_gz_d10 = s.gz;
    last_ts_ms_print = s.ts_ms;
  }

  // 1 Hz debug print: always print once per second to prove liveness, even if no packs occurred
  tick_ppg_accum += ppg_services;
  tick_imu_accum += imu_packs;
  if ((millis() - last_debug_ms) >= 1000) {
    const char* temp_str = "n/a";
    char temp_buf[16];
    if (isfinite(last_temp_c)) {
      float tf = c_to_f(last_temp_c);
      snprintf(temp_buf, sizeof(temp_buf), "%.2f", tf);
      temp_str = temp_buf;
    }
    uint32_t ppg_irq_hz = ppg_irq_count - ppg_irq_count_last; ppg_irq_count_last = ppg_irq_count;
    uint32_t ppg_samp_hz = ppg_sample_count - ppg_sample_count_last; ppg_sample_count_last = ppg_sample_count;
    uint32_t imu_hz = imu_sample_count - imu_sample_count_last; imu_sample_count_last = imu_sample_count;
    uint32_t ppg_tick_hz = tick_ppg_accum; tick_ppg_accum = 0;
    uint32_t imu_tick_hz = tick_imu_accum; tick_imu_accum = 0;
    // Choose a sample to show: last packed or synthesize from caches
    uint16_t hr_print = (uint16_t)lroundf(last_bpm);
    int16_t axp = last_ax_mg, ayp = last_ay_mg, azp = last_az_mg;
    int16_t gxp = last_gx_d10, gyp = last_gy_d10, gzp = last_gz_d10;
    unsigned long tsp = last_ts_ms_print ? last_ts_ms_print : millis();
    Serial.printf(
      "HR=%u BPM, Temp=%s F, A=[%d %d %d] mg, G=[%d %d %d] dpsx10, ts=%lu ms, idx=%u/%u, rb=%u pages | rates: PPG_irq=%lu Hz, PPG_samp=%lu Hz, IMU=%lu Hz | ticks: PPG=%lu Hz, IMU=%lu Hz\n",
      (unsigned)hr_print,
      temp_str,
      (int)axp, (int)ayp, (int)azp,
      (int)gxp, (int)gyp, (int)gzp,
      (unsigned long)tsp,
      (unsigned)sample_index, (unsigned)kSamplesPerPage,
      (unsigned)reg_buffer::size(),
      (unsigned long)ppg_irq_hz, (unsigned long)ppg_samp_hz, (unsigned long)imu_hz,
      (unsigned long)ppg_tick_hz, (unsigned long)imu_tick_hz
    );
    last_debug_ms = millis();
  }
}
