// src/aht20_demo.cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>

// Pins for your ESP32 I2C bus
static constexpr int SDA_PIN = 21;
static constexpr int SCL_PIN = 22;

Adafruit_AHTX0 aht;

// Test configuration
static constexpr unsigned long QUICK_READS = 5;          // quick sanity reads
static constexpr unsigned long STABILITY_SECS = 15;      // duration for stability test
static constexpr unsigned long STABILITY_RATE_MS = 500;  // sample period during stability test (~2 Hz)

struct Stats { float min, max, avg, stddev; int samples; int invalid; };

void i2cScan() {
  Serial.println("I2C scan...");
  byte count = 0;
  for (byte addr = 0x08; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    byte err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("  - Found device at 0x%02X\n", addr);
      count++;
    }
  }
  if (count == 0) Serial.println("  - No I2C devices found");
}

bool readOnce(float &tc, float &rh, unsigned long &elapsed_ms) {
  unsigned long t0 = millis();
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp); // This triggers a measurement and reads it
  elapsed_ms = millis() - t0;
  tc = temp.temperature;
  rh = humidity.relative_humidity;
  bool ok = isfinite(tc) && isfinite(rh);
  return ok;
}

Stats runStabilityTest(unsigned long duration_ms, unsigned long period_ms) {
  Stats st{};
  st.min =  1e9f;
  st.max = -1e9f;
  double sum = 0.0, sumsq = 0.0;

  unsigned long t_start = millis();
  unsigned long next = t_start;
  while (millis() - t_start < duration_ms) {
    float tc, rh; unsigned long dt;
    bool ok = readOnce(tc, rh, dt);
    if (!ok || isnan(tc) || isnan(rh) || rh < 0.0f || rh > 100.0f || tc < -40.0f || tc > 85.0f) {
      st.invalid++;
    } else {
      // Track stats on both channels separately? We'll compute combined via temperature
      // and humidity separately for display, but here keep temperature stats only; humidity we'll print min/max separately.
      if (tc < st.min) st.min = tc;
      if (tc > st.max) st.max = tc;
      sum += tc;
      sumsq += (double)tc * (double)tc;
      st.samples++;
      Serial.printf("  T=%.2fC (%.2fF)  RH=%.2f%%  (read=%lums)\n", tc, tc*9/5+32, rh, dt);
    }
    next += period_ms;
    long wait = (long)next - (long)millis();
    if (wait > 0) delay((unsigned long)wait);
  }

  if (st.samples > 0) {
    st.avg = (float)(sum / st.samples);
    double var = (sumsq / st.samples) - ((double)st.avg * (double)st.avg);
    st.stddev = (float)((var > 0) ? sqrt(var) : 0.0);
  } else {
    st.min = st.max = st.avg = st.stddev = NAN;
  }
  return st;
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("===== AHT20 Test Suite =====");

  Wire.begin(SDA_PIN, SCL_PIN);
  i2cScan();

  Serial.print("Init AHT20: ");
  bool ok = aht.begin();
  Serial.println(ok ? "OK" : "FAIL");
  if (!ok) {
    Serial.println("Sensor not detected. Check wiring and address (default 0x38). Halting.");
    while (1) delay(50);
  }

  // Quick sanity reads
  Serial.println("-- Quick reads --");
  int quick_ok = 0; unsigned long tmin=9999, tmax=0; double tsum=0;
  for (unsigned long i = 0; i < QUICK_READS; ++i) {
    float tc, rh; unsigned long dt;
    bool rok = readOnce(tc, rh, dt);
    if (rok) {
      quick_ok++;
      if (dt < tmin) tmin = dt;
      if (dt > tmax) tmax = dt;
      tsum += dt;
      Serial.printf("  #%lu: T=%.2fC (%.2fF) RH=%.2f%%  (read=%lums)\n", i+1, tc, tc*9/5+32, rh, dt);
    } else {
      Serial.printf("  #%lu: read FAILED\n", i+1);
    }
    delay(100);
  }
  double tavg = (quick_ok > 0) ? (tsum / quick_ok) : NAN;
  Serial.printf("Quick reads: %d/%lu OK  read-time ms (min/avg/max) = %lu / %.1f / %lu\n",
                quick_ok, QUICK_READS, tmin, tavg, tmax);

  // Stability test
  Serial.println("-- Stability test --");
  Stats st = runStabilityTest(STABILITY_SECS * 1000UL, STABILITY_RATE_MS);
  Serial.printf("Samples OK=%d Invalid=%d\n", st.samples, st.invalid);
  Serial.printf("Temperature stats: min=%.2fC max=%.2fC avg=%.2fC std=%.2f\n", st.min, st.max, st.avg, st.stddev);

  // Plausibility checks
  bool pass_quick = (quick_ok == (int)QUICK_READS);
  bool pass_stability = (st.samples > 0 && st.invalid == 0);
  bool pass_ranges = !(st.min < -40.0f || st.max > 85.0f);

  Serial.println("-- Summary --");
  Serial.printf("Init: %s\n", ok ? "PASS" : "FAIL");
  Serial.printf("Quick reads: %s\n", pass_quick ? "PASS" : "FAIL");
  Serial.printf("Stability: %s\n", pass_stability ? "PASS" : "FAIL");
  Serial.printf("Range: %s\n", pass_ranges ? "PASS" : "FAIL");

  bool overall = ok && pass_quick && pass_stability && pass_ranges;
  Serial.printf("OVERALL: %s\n", overall ? "PASS" : "FAIL");

  Serial.println();
  Serial.println("Entering monitor mode (1 Hz). Press reset to rerun tests.");
}

void loop() {
  static unsigned long last = 0;
  unsigned long now = millis();
  if (now - last >= 1000) {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    Serial.printf("AHT20: T=%.2f C (%.2f F)  RH=%.2f%%\n",
                  temp.temperature, temp.temperature*9/5+32, humidity.relative_humidity);
    last = now;
  }
}
