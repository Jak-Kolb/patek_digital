#include <Arduino.h>
#include <Wire.h>

// BMI270
#include "SparkFun_BMI270_Arduino_Library.h"
// MAX30102
#include "MAX30105.h"
#include "heartRate.h"
// ring buffer for inter-subsystem comms
#include "reg_buffer.h"
#include <cmath>

static constexpr int SDA_PIN = 21;
static constexpr int SCL_PIN = 22;

BMI270 imu;
MAX30105 ppg;

bool haveIMU = false;
bool havePPG = false;

// sequence counter for ringbuffer pages
static uint32_t hr_seq = 0;

// Helper: pack a 256-byte page with a tiny header and the median BPM
static void push_hr_median_to_ring(int med_bpm) {
  uint8_t page[REG_BUFFER_PAGE_BYTES];
  // zero page
  memset(page, 0, sizeof(page));
  // header (bytes 0..3): 'H','R', seq_low, seq_high (simple)
  page[0] = 'H';
  page[1] = 'R';
  page[2] = (uint8_t)(hr_seq & 0xFF);
  page[3] = (uint8_t)((hr_seq >> 8) & 0xFF);
  // payload: at offset 16 put med BPM (int16) and timestamp (uint32)
  size_t off = 16;
  int16_t bpm16 = (int16_t)med_bpm;
  memcpy(&page[off], &bpm16, sizeof(bpm16));
  // store a 32-bit millis() snapshot after BPM
  uint32_t t = (uint32_t)millis();
  memcpy(&page[off + 2], &t, sizeof(t));

  // push to ring buffer (overwrites oldest when full)
  reg_buffer::push_256(page);
  hr_seq++;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BMI270 + MAX30102 dual demo]");

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);  // drop to 100k if your wiring is flaky

  // --- BMI270 ---
  if (imu.beginI2C(0x68, Wire) == BMI2_OK) {
    haveIMU = true;
    imu.setAccelODR(BMI2_ACC_ODR_100HZ);
    imu.setGyroODR(BMI2_GYR_ODR_100HZ);
    Serial.println("BMI270 OK @0x68");
  } else {
    Serial.println("BMI270 NOT FOUND");
  }

  // --- MAX30102 ---
  if (ppg.begin(Wire, I2C_SPEED_FAST)) {
    havePPG = true;
    // Minimal stable config (match single-sensor demo for reliable HR)
    ppg.setup();                       // default config from lib
  ppg.setPulseAmplitudeRed(0x0A);    // visible “alive” glow
  ppg.setPulseAmplitudeIR(0x20);     // main signal - increase if you see very small IR values
    ppg.setPulseAmplitudeGreen(0x00);  // off
    ppg.setSampleRate(100);
  // Use SparkFun library constants for pulse width and ADC range
  ppg.setPulseWidth(0x03);           // MAX30105_PULSEWIDTH_411 (18-bit, widest pulse)
  ppg.setADCRange(0x60);             // MAX30105_ADCRANGE_16384 (largest range)
    Serial.println("MAX30102 OK @0x57");
  } else {
    Serial.println("MAX30102 NOT FOUND");
  }
}

void loop() {
  // --- BMI270 read ---
  float ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
  if (haveIMU) {
    if (imu.getSensorData() == BMI2_OK) {
      ax = imu.data.accelX;  // g
      ay = imu.data.accelY;
      az = imu.data.accelZ;
      gx = imu.data.gyroX;   // deg/s
      gy = imu.data.gyroY;
      gz = imu.data.gyroZ;
    } else {
      Serial.println("[warn] BMI270 read failed");
    }
  }

  // --- MAX30102 read + simple HR (mirror max30102_demo_simple.cpp exactly)
  static long lastBeat = 0;
  static const byte RATE_SIZE = 6; // match simple demo
  static byte rates[RATE_SIZE] = {0};
  static byte rateSpot = 0;
  static byte beatCount = 0; // how many valid beats stored (<= RATE_SIZE)
  float beatsPerMinute = 0.0f;
  int beatAvg = 0;

  // smoothing and display variables (same behavior as simple demo)
  const bool PLOT_MODE = false;
  const int IR_SMOOTH = 5;
  static unsigned long ir_window[IR_SMOOTH];
  static int ir_idx = 0;
  static bool ir_filled = false;
  static long raw_ir = 0, red = 0;

  if (havePPG) {
    // Wait for a new sample (identical to simple demo's behavior)
    while (ppg.available() == false) ppg.check();
    raw_ir = ppg.getIR();
    red = ppg.getRed();
    ppg.nextSample();

    // rolling window average for display/plotting
    ir_window[ir_idx++] = raw_ir;
    if (ir_idx >= IR_SMOOTH) ir_idx = 0;
    if (!ir_filled && ir_idx == 0) ir_filled = true;
    unsigned long ir_sum = 0;
    int ir_count = ir_filled ? IR_SMOOTH : ir_idx;
    for (int i = 0; i < ir_count; ++i) ir_sum += ir_window[i];
    long irValue = (ir_count > 0) ? (long)(ir_sum / ir_count) : raw_ir;

    if (PLOT_MODE) {
      Serial.println(irValue);
    } else {
      // Use the raw sample for beat detection to avoid smoothing blunting peaks
      if (checkForBeat(raw_ir) == true) {
        // We sensed a beat!
        long delta = millis() - lastBeat;
        lastBeat = millis();

        beatsPerMinute = 60 / (delta / 1000.0);

        if (beatsPerMinute < 255 && beatsPerMinute > 20) {
          rates[rateSpot++] = (byte)beatsPerMinute;  // Store this reading in the array
          if (rateSpot >= RATE_SIZE) rateSpot = 0;
          if (beatCount < RATE_SIZE) beatCount++;

          // Take median of stored readings for robust smoothing
          int tmp[RATE_SIZE];
          int start = (rateSpot - beatCount + RATE_SIZE) % RATE_SIZE;
          for (int i = 0; i < beatCount; ++i) tmp[i] = rates[(start + i) % RATE_SIZE];
          // simple sort
          for (int i = 0; i < beatCount - 1; ++i) for (int j = i+1; j < beatCount; ++j) if (tmp[j] < tmp[i]) { int t=tmp[i]; tmp[i]=tmp[j]; tmp[j]=t; }
          beatAvg = tmp[beatCount/2];

          // push median to ring buffer when we have a full window
          if (beatCount >= RATE_SIZE) {
            push_hr_median_to_ring(beatAvg);
            Serial.printf("[PPG] Median BPM pushed: %d\n", beatAvg);
          }
        }
        Serial.printf("[PPG] Beat detected: IR=%ld RED=%ld BPM=%.1f Avg=%d\n", raw_ir, red, beatsPerMinute, beatAvg);
      }
    }
  }

  // Prefer instantaneous BPM (beatsPerMinute) but fall back to the recent average
  // so the serial output shows a meaningful value as soon as one is available.
  float displayBPM = beatsPerMinute;
  if (displayBPM == 0.0f && beatAvg > 0) displayBPM = (float)beatAvg;

  // Print one combined line
  // Compute P2P quality from the rolling buffer (if used)
  static const int P2P_WINDOW = 50;
  static long p2p_buf[P2P_WINDOW];
  static int p2p_idx = 0;
  static bool p2p_filled = false;
  // update rolling buffer with current raw_ir
  p2p_buf[p2p_idx++] = raw_ir;
  if (p2p_idx >= P2P_WINDOW) { p2p_idx = 0; p2p_filled = true; }
  int p2p_count = p2p_filled ? P2P_WINDOW : p2p_idx;
  long pmin = LONG_MAX, pmax = LONG_MIN;
  for (int i = 0; i < p2p_count; ++i) { if (p2p_buf[i] < pmin) pmin = p2p_buf[i]; if (p2p_buf[i] > pmax) pmax = p2p_buf[i]; }
  long p2p = (p2p_count > 0) ? (pmax - pmin) : 0;

  // decide if finger present based on P2P amplitude (tunable)
  bool finger = (p2p > 50);

  Serial.printf(
    "IMU: ax=%.3f ay=%.3f az=%.3f g  |  gx=%.1f gy=%.1f gz=%.1f dps  ||  "
    "PPG: IR=%ld RED=%ld BPM=%.1f Med=%d P2P=%ld%s\n",
    ax, ay, az, gx, gy, gz,
  raw_ir, red, displayBPM, beatAvg, p2p,
    (havePPG && !finger ? " (no finger or low signal)" : "")
  );

  // Match single-sensor demo loop timing; faster loop helps beat detection
  // when processing available FIFO samples.
  delay(50);
}
