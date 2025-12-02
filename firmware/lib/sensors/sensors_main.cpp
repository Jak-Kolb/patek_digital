// Phase 2: Integrate sensor sampling at precise rates using hardware timers.
//  - IMU: 100 Hz
//  - PPG (MAX30102): 50 Hz
//  - Temperature (MAX30205): 1 Hz
// We perform I2C reads in the main loop when flags set by ISRs to keep ISRs fast.
// Each 1 second window we print the averaged values over the prior second.

#include <Arduino.h>
#include <Wire.h>
#include <ctime>
#include <sys/time.h>
#include "sensors.h"
#include "app_config.h"
#include <SparkFun_BMI270_Arduino_Library.h>
#include "ringbuf/reg_buffer.h"

// Addresses (adapted from sensors_demo.cpp)
static constexpr uint8_t BMI270_ADDR      = 0x68;
static constexpr uint8_t BMI270_ADDR_ALT  = 0x69;
static constexpr uint8_t MAX30102_ADDR    = 0x57;
static constexpr uint8_t MAX30205_ADDR    = 0x48; // single address variant used

// --- Minimal I2C helpers (blocking; acceptable for demo) ---
static bool i2c_write8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
static int i2c_read8(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((int)addr, 1) != 1) return -1;
  return Wire.read();
}
static size_t i2c_readN(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  size_t got = Wire.requestFrom((int)addr, (int)n);
  for (size_t i = 0; i < got; ++i) buf[i] = Wire.read();
  return got;
}
static bool i2c_ping(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

// --- BMI270 ---
static BMI270 g_imu;
static bool   g_bmi_ok = false;
static bool bmi270_begin() {
  if (g_imu.beginI2C(BMI270_ADDR, Wire) != BMI2_OK) {
    if (g_imu.beginI2C(BMI270_ADDR_ALT, Wire) != BMI2_OK) {
      Serial.println("BMI270: not found");
      g_bmi_ok = false; return false;
    }
  }
  int8_t rs;
  rs = g_imu.setAccelODR(BMI2_ACC_ODR_100HZ);
  if (rs != BMI2_OK) Serial.printf("BMI270 accel ODR fail (%d)\n", rs);
  rs = g_imu.setGyroODR(BMI2_GYR_ODR_100HZ);
  if (rs != BMI2_OK) Serial.printf("BMI270 gyro ODR fail (%d)\n", rs);
  g_bmi_ok = true;
  Serial.println("BMI270 ready");
  return true;
}
struct ImuSample { float ax, ay, az; float gx, gy, gz; float tempC; bool ok; };
static ImuSample bmi270_read() {
  ImuSample s{0,0,0,0,0,0,NAN,false};
  if (!g_bmi_ok) return s;
  if (g_imu.getSensorData() != BMI2_OK) return s;
  s.ax = g_imu.data.accelX; s.ay = g_imu.data.accelY; s.az = g_imu.data.accelZ;
  s.gx = g_imu.data.gyroX;  s.gy = g_imu.data.gyroY;  s.gz = g_imu.data.gyroZ;
  float t; if (g_imu.getTemperature(&t) == BMI2_OK) s.tempC = t; else s.tempC = NAN;
  s.ok = true;
  return s;
}

// --- MAX30102 (configure, drain FIFO like sensors_demo) ---
static constexpr uint8_t MAX30102_REG_INT_STATUS_1 = 0x00;
static constexpr uint8_t MAX30102_REG_INT_STATUS_2 = 0x01;
static constexpr uint8_t MAX30102_REG_INT_ENABLE_1 = 0x02;
static constexpr uint8_t MAX30102_REG_INT_ENABLE_2 = 0x03;
static constexpr uint8_t MAX30102_REG_FIFO_WR_PTR  = 0x04;
static constexpr uint8_t MAX30102_REG_OVF_COUNTER  = 0x05;
static constexpr uint8_t MAX30102_REG_FIFO_RD_PTR  = 0x06;
static constexpr uint8_t MAX30102_REG_FIFO_DATA    = 0x07;
static constexpr uint8_t MAX30102_REG_FIFO_CONFIG  = 0x08;
static constexpr uint8_t MAX30102_REG_MODE_CONFIG  = 0x09;
static constexpr uint8_t MAX30102_REG_SPO2_CONFIG  = 0x0A;
static constexpr uint8_t MAX30102_REG_LED1_PA      = 0x0C;
static constexpr uint8_t MAX30102_REG_LED2_PA      = 0x0D;
static constexpr uint8_t MAX30102_REG_PART_ID      = 0xFF;

static bool max30102_begin() {
  if (!i2c_ping(MAX30102_ADDR)) { Serial.println("MAX30102: not found"); return false; }
  int part = i2c_read8(MAX30102_ADDR, MAX30102_REG_PART_ID);
  if (part != 0x15) { Serial.printf("MAX30102: unexpected PART_ID=0x%02X\n", part); return false; }
  Serial.printf("MAX30102: found PART_ID=0x%02X\n", part);
  // Reset & wait
  (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_MODE_CONFIG, 0x40);
  uint32_t t0 = millis();
  while (millis() - t0 < 200) {
    int m = i2c_read8(MAX30102_ADDR, MAX30102_REG_MODE_CONFIG);
    if (m >= 0 && (m & 0x40) == 0) break;
    delay(2);
  }
  // Clear status
  (void)i2c_read8(MAX30102_ADDR, MAX30102_REG_INT_STATUS_1);
  (void)i2c_read8(MAX30102_ADDR, MAX30102_REG_INT_STATUS_2);
  // Reset FIFO pointers
  (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_FIFO_WR_PTR, 0x00);
  (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_OVF_COUNTER, 0x00);
  (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_FIFO_RD_PTR, 0x00);
  // FIFO config: avg=4, rollover enable, threshold 0x0F
  (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_FIFO_CONFIG, 0b01010000 | 0x0F);
  // SpO2 config: ADC range 4096nA, ~100Hz sample rate, pulse width 411us
  (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_SPO2_CONFIG, 0b11001111);
  // LED currents (slightly higher for better SNR)
  (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_LED1_PA, 0x28);
  (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_LED2_PA, 0x28);
  // Enable PPG ready interrupt (not strictly used but set for completeness)
  (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_INT_ENABLE_1, 0x40);
  // Mode: SpO2 (RED+IR)
  (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_MODE_CONFIG, 0x03);
  delay(50);
  Serial.println("MAX30102 ready");
  return true;
}
struct PpgSample { uint32_t red; uint32_t ir; bool ok; };
static PpgSample max30102_readOne() {
  PpgSample s{0,0,false};
  int wr = i2c_read8(MAX30102_ADDR, MAX30102_REG_FIFO_WR_PTR);
  int rd = i2c_read8(MAX30102_ADDR, MAX30102_REG_FIFO_RD_PTR);
  if (wr < 0 || rd < 0) return s;
  uint8_t available = (uint8_t)((wr - rd) & 0x1F);
  if (!available) return s;
  if (available > 32) available = 32; // safety cap
  for (uint8_t i = 0; i < available; ++i) {
    uint8_t data[6];
    if (i2c_readN(MAX30102_ADDR, MAX30102_REG_FIFO_DATA, data, 6) != 6) { s.ok = false; return s; }
    uint32_t red = ((uint32_t)(data[0] & 0x03) << 16) | ((uint32_t)data[1] << 8) | data[2];
    uint32_t ir  = ((uint32_t)(data[3] & 0x03) << 16) | ((uint32_t)data[4] << 8) | data[5];
    s.red = red; s.ir = ir; s.ok = true; // keep last
  }
  return s;
}

// --- MAX30205 ---
static bool max30205_ok = false;
static bool max30205_begin() {
  if (!i2c_ping(MAX30205_ADDR)) { Serial.println("MAX30205: not found"); return false; }
  max30205_ok = true; Serial.println("MAX30205 ready"); return true;
}
static bool max30205_readTemp(float &c) {
  if (!max30205_ok) return false;
  uint8_t buf[2];
  if (i2c_readN(MAX30205_ADDR, 0x00, buf, 2) != 2) return false;
  int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
  c = raw / 256.0f;
  return true;
}

// --- Timer handles ---
static hw_timer_t* tImu  = nullptr;
static hw_timer_t* tPpg  = nullptr;
static hw_timer_t* tTemp = nullptr;

// --- ISR flags ---
static volatile bool imuDue  = false;
static volatile bool ppgDue  = false;
static volatile bool tempDue = false; // also marks second boundary
static volatile bool secondFlag = false;

// --- Accumulators for 1s window ---
static uint32_t imuCount = 0; static double axSum=0, aySum=0, azSum=0, gxSum=0, gySum=0, gzSum=0, imuTempSumF=0;
static uint32_t ppgCount = 0; static double redSum=0, irSum=0;
static uint32_t tempCount = 0; static double bodyTempCSum=0, bodyTempFSum=0;

// --- 500ms window accumulators ---
static uint32_t halfImuCount = 0; static double halfAxSum=0, halfAySum=0, halfAzSum=0, halfGxSum=0, halfGySum=0, halfGzSum=0, halfImuTempSumF=0;
static uint32_t halfPpgCount = 0; static double halfRedSum=0, halfIrSum=0;
static uint32_t halfTempCount = 0; static double halfBodyTempCSum=0; // store C only; F derived when packing
static uint32_t halfWindowStartMs = 0; // initialized in setup

// --- Ring buffer target ---
static reg_buffer::SampleRingBuffer* g_targetBuffer = nullptr;

// --- Barebones heart rate estimation state ---
static float hrBpm = 0.0f;            // latest BPM (unsmoothed)
static uint32_t lastBeatMs = 0;       // timestamp of last beat (ms)

// Very simple beat detection: baseline + relative threshold, rising edge, refractory.
static bool simpleBeatDetect(uint32_t ir)
{
  static float baseline = 0.0f;                // adaptive DC baseline
  if (baseline == 0.0f) baseline = (float)ir;   // initialize to first reading
  // Faster adaptation reduces diff amplitude (fewer false peaks at rest)
  baseline = 0.98f * baseline + 0.02f * ir;    // ~50 sample time constant (@55Hz ≈0.9s)
  float diff = (float)ir - baseline;
  float threshold = baseline * 0.004f;         // keep same relative threshold
  static bool prevAbove = false;
  uint32_t now = millis();
  const uint32_t REFRACTORY_MS = 500;          // ignore beats faster than 120 BPM
  bool rising = (diff > threshold) && !prevAbove;
  bool beat = false;
  if (rising && (now - lastBeatMs) > REFRACTORY_MS) {
    beat = true;
    uint32_t delta = now - lastBeatMs;
    lastBeatMs = now;
    if (delta > 600 && delta < 2000) {         // plausible 30-100 BPM
      hrBpm = 60000.0f / (float)delta;
    }
  }
  prevAbove = diff > threshold;
  return beat;
}

void IRAM_ATTR onImuTimer()  { imuDue  = true; }
void IRAM_ATTR onPpgTimer()  { ppgDue  = true; }
void IRAM_ATTR onTempTimer() { tempDue = true; secondFlag = true; }

static hw_timer_t* setupTimer(uint8_t id, uint16_t divider, uint64_t periodUs, void (*isr)()) {
  hw_timer_t* t = timerBegin(id, divider, true);
  timerAttachInterrupt(t, isr, true);
  timerAlarmWrite(t, periodUs, true);
  timerAlarmEnable(t);
  return t;
}

void sensors_setup(reg_buffer::SampleRingBuffer* buffer) {
  g_targetBuffer = buffer;
  // Serial.begin(115200);
  // delay(500);
  Serial.println("\nTimed sensor sampling demo (Phase 2)");
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_CLOCK_HZ); // full speed for sensor throughput; adjust if bus stability issues
  delay(10);
  (void)bmi270_begin();
  (void)max30102_begin();
  (void)max30205_begin();
  // Configure timers: APB 80MHz / divider 80 = 1MHz tick
  // Target 25 Hz = 40000 us
  tImu  = setupTimer(0, 80, 40000,    onImuTimer);   // 25 Hz
  tPpg  = setupTimer(1, 80, 40000,    onPpgTimer);   // 25 Hz
  tTemp = setupTimer(2, 80, 1000000,  onTempTimer);  // 1 Hz
  halfWindowStartMs = millis();
}

static float lastBodyTempC = 0.0f;

static void sampleImu() {
  ImuSample s = bmi270_read();
  if (!s.ok) return;
  
  // Push raw sample to ring buffer immediately
  if (g_targetBuffer) {
    reg_buffer::Sample rs{};
    rs.ax = (reg_buffer::float16)s.ax;
    rs.ay = (reg_buffer::float16)s.ay;
    rs.az = (reg_buffer::float16)s.az;
    rs.gx = (reg_buffer::float16)s.gx;
    rs.gy = (reg_buffer::float16)s.gy;
    rs.gz = (reg_buffer::float16)s.gz;
    rs.hr_bpm = (reg_buffer::float16)(hrBpm > 0 ? hrBpm : 0);
    rs.temp_c = (reg_buffer::float16)lastBodyTempC;
    
    const time_t t_now = time(nullptr);
    if (t_now > 1000000000) {
        rs.epoch_min = (float)(t_now / 60);
    } else {
        rs.epoch_min = (float)millis() / 60000.f;
    }
    
    if (!g_targetBuffer->push(rs)) {
      // Serial.println("Ring buffer full; sample dropped");
    }
  }

  axSum += s.ax; aySum += s.ay; azSum += s.az;
  gxSum += s.gx; gySum += s.gy; gzSum += s.gz;
  if (!isnan(s.tempC)) imuTempSumF += (s.tempC * 1.8 + 32.0);
  imuCount++;
}
static void samplePpg() {
  PpgSample p = max30102_readOne();
  if (!p.ok) return;
  redSum += p.red; irSum += p.ir; ppgCount++;
  (void)simpleBeatDetect(p.ir); // updates hrBpm internally
}
static void sampleTemp() {
  float c; if (!max30205_readTemp(c)) return; 
  lastBodyTempC = c;
  bodyTempCSum += c; bodyTempFSum += (c * 9.0/5.0 + 32.0); tempCount++; 
}

void sensors_loop() {
  // Service due flags (keep ISRs minimal)
  if (imuDue) { imuDue = false; sampleImu(); }
  if (ppgDue) { ppgDue = false; samplePpg(); }
  if (tempDue) { tempDue = false; sampleTemp(); }

  if (secondFlag) {
    secondFlag = false;
    // Compute averages
    double axAvg = imuCount ? axSum / imuCount : NAN;
    double ayAvg = imuCount ? aySum / imuCount : NAN;
    double azAvg = imuCount ? azSum / imuCount : NAN;
    double gxAvg = imuCount ? gxSum / imuCount : NAN;
    double gyAvg = imuCount ? gySum / imuCount : NAN;
    double gzAvg = imuCount ? gzSum / imuCount : NAN;
    double imuTempFAvg = (imuCount && imuTempSumF>0) ? imuTempSumF / imuCount : NAN;
    double redAvg = ppgCount ? redSum / ppgCount : NAN;
    double irAvg  = ppgCount ? irSum  / ppgCount : NAN;
    double bodyTCAvg = tempCount ? bodyTempCSum / tempCount : NAN;
    double bodyTFAvg = tempCount ? bodyTempFSum / tempCount : NAN;

    Serial.printf("1s AVG IMU at sample rate %uHz (target 25) a[g]=[% .3f % .3f % .3f] g[dps]=[% .2f % .2f % .2f]", imuCount, axAvg, ayAvg, azAvg, gxAvg, gyAvg, gzAvg);
    if (!isnan(imuTempFAvg)) Serial.printf(" imuT=%.1fF", imuTempFAvg);
    Serial.print("\n");
    Serial.printf("1s AVG PPG at sample rate %uHz (target 25) RED=%.0f IR=%.0f\n", ppgCount, redAvg, irAvg);
    Serial.printf("HR=%.1f BPM\n", hrBpm);
    if (!isnan(bodyTCAvg)) {
      Serial.printf("1s AVG BodyTemp at sample rate %uHz: %.2fC (%.2fF)\n", tempCount, bodyTCAvg, bodyTFAvg);
    } else {
      Serial.println("1s AVG BodyTemp: no samples");
    }
    Serial.println("---");
    // Reset accumulators for next window
    axSum=aySum=azSum=gxSum=gySum=gzSum=imuTempSumF=0; imuCount=0;
    redSum=irSum=0; ppgCount=0;
    bodyTempCSum=bodyTempFSum=0; tempCount=0;
  }
  // Small delay to yield; all real work is event-driven
  delay(1);
}
