#include <Arduino.h>
#include <Wire.h>
#include "app_config.h"  // Provides I2C_SDA_PIN / I2C_SCL_PIN (and optional BMI270_INT_PIN)
#include <SparkFun_BMI270_Arduino_Library.h>

// I2C addresses (adjust if your hardware differs)
static constexpr uint8_t BMI270_ADDR   = 0x68; // IMU (SDO low)
static constexpr uint8_t BMI270_ADDR_ALT = 0x69; // IMU (SDO high)
static constexpr uint8_t MAX30102_ADDR = 0x57; // Heart-rate sensor
// MAX30205 temp sensor can be 0x48..0x4B depending on A0/A1 pins; we'll scan these
static constexpr uint8_t MAX30205_ADDR = 0x48;

// ---- Small I2C helpers (robust and concise) ----
static bool i2c_write8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static int i2c_read8(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1; // repeated start
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

// ---- BMI270 using SparkFun library (robust) ----
static BMI270 g_imu;
static bool   g_bmi_ok = false;

// Try to power on accel/gyro. SparkFun lib loads the BMI270 config file internally.
static bool bmi270_begin() {
  // Try default address then alternate
  if (g_imu.beginI2C(BMI270_ADDR, Wire) != BMI2_OK) {
    if (g_imu.beginI2C(BMI270_ADDR_ALT, Wire) != BMI2_OK) {
      Serial.println("BMI270: not found at 0x68/0x69");
      g_bmi_ok = false;
      return false;
    }
  }
  // Set reasonable ODRs; library handles config & firmware
    int8_t rs;

    rs = g_imu.setAccelODR(BMI2_ACC_ODR_100HZ);
    if (rs != BMI2_OK) Serial.printf("BMI270: setAccelODR failed (%d)\n", rs);

    rs = g_imu.setGyroODR(BMI2_GYR_ODR_100HZ);
    if (rs != BMI2_OK) Serial.printf("BMI270: setGyroODR failed (%d)\n", rs);

    g_bmi_ok = true;
    Serial.println("BMI270 initialized via SparkFun library");
    return true;
}

struct ImuSample {
  float ax_g, ay_g, az_g;
  float gx_dps, gy_dps, gz_dps;
  float temp_c;
  bool  ok;
};

static ImuSample bmi270_read() {
  ImuSample s{};
  if (!g_bmi_ok) { s.ok = false; return s; }
  if (g_imu.getSensorData() != BMI2_OK) { s.ok = false; return s; }
  const auto &d = g_imu.data; // BMI270_SensorData
  s.ax_g = d.accelX;
  s.ay_g = d.accelY;
  s.az_g = d.accelZ;
  s.gx_dps = d.gyroX;
  s.gy_dps = d.gyroY;
  s.gz_dps = d.gyroZ;
  float t;
if (g_imu.getTemperature(&t) == BMI2_OK) {
  s.temp_c = t;
} else {
  s.temp_c = NAN;
}
  s.ok = true;
  return s;
}

// ---- MAX30102 (PPG heart-rate sensor) ----
// Key registers
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
static constexpr uint8_t MAX30102_REG_LED1_PA      = 0x0C; // RED
static constexpr uint8_t MAX30102_REG_LED2_PA      = 0x0D; // IR
static constexpr uint8_t MAX30102_REG_PART_ID      = 0xFF; // expect 0x15

static bool max30102_begin() {
    if (!i2c_ping(MAX30102_ADDR)) {
        Serial.println("MAX30102: not found");
        return false;
    }
    int part = i2c_read8(MAX30102_ADDR, MAX30102_REG_PART_ID);
    if (part != 0x15) {
        Serial.printf("MAX30102: unexpected PART_ID=0x%02X\n", part);
        return false;
    }
    else {
        Serial.printf("MAX30102: found PART_ID=0x%02X\n", part);
    }

  // Reset device and wait until RESET bit clears
    (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_MODE_CONFIG, 0x40);
    {
        uint32_t t0 = millis();
        while (millis() - t0 < 200) {
            int m = i2c_read8(MAX30102_ADDR, MAX30102_REG_MODE_CONFIG);
            if (m >= 0 && (m & 0x40) == 0) break;
            delay(2);
        }
    }

    // Clear any latched interrupts by reading status registers
    (void)i2c_read8(MAX30102_ADDR, MAX30102_REG_INT_STATUS_1);
    (void)i2c_read8(MAX30102_ADDR, MAX30102_REG_INT_STATUS_2);

    // Clear FIFO
    (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_FIFO_WR_PTR, 0x00);
    (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_OVF_COUNTER, 0x00);
    (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_FIFO_RD_PTR, 0x00);
    // FIFO config: sample avg=4 (0b010<<5), rollover enabled (bit4), almost-full threshold = 0x0F
    (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_FIFO_CONFIG, 0b01010000 | 0x0F); // 0x5F
    // SpO2 config: ADC range 4096nA (0b11<<5), sample rate ~100Hz (0b011<<2), pulse width 411us (0b11)
    (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_SPO2_CONFIG, 0b11001111); // 0xCF
    // LED currents (tune for your board/sensor contact)
    (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_LED1_PA, 0x28); // RED ~8.6mA
    (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_LED2_PA, 0x28); // IR  ~8.6mA
    // Enable PPG_RDY interrupt (optional; we still rely on FIFO pointers)
    (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_INT_ENABLE_1, 0x40);
    // Mode: SpO2 (uses RED+IR)
    (void)i2c_write8(MAX30102_ADDR, MAX30102_REG_MODE_CONFIG, 0x03);
    // Allow time for first sample to be produced
    delay(50);
    return true;
}

struct PpgSample {
  uint32_t red;
  uint32_t ir;
  bool ok;
};

static PpgSample max30102_readOne() {
    PpgSample s{};
    // Check FIFO pointer difference only (avoid clearing status flags unnecessarily)
    int wr = i2c_read8(MAX30102_ADDR, MAX30102_REG_FIFO_WR_PTR);
    int rd = i2c_read8(MAX30102_ADDR, MAX30102_REG_FIFO_RD_PTR);
    if (wr < 0 || rd < 0) { 
        s.ok = false; return s; 
    }
    uint8_t available = (uint8_t)((wr - rd) & 0x1F);
    if (available == 0) { 
        s.ok = false; return s; 
    }
    if (available > 32) {
        available = 32;   
    }
    // Drain all available samples; keep the most recent one
    for (uint8_t i = 0; i < available; ++i) {
        uint8_t data[6];
        if (i2c_readN(MAX30102_ADDR, MAX30102_REG_FIFO_DATA, data, 6) != 6) { s.ok = false; return s; }
        uint32_t red = ((uint32_t)(data[0] & 0x03) << 16) | ((uint32_t)data[1] << 8) | data[2];
        uint32_t ir  = ((uint32_t)(data[3] & 0x03) << 16) | ((uint32_t)data[4] << 8) | data[5];
        // Keep last sample
        s.red = red;
        s.ir  = ir;
        s.ok  = true;
    }
    return s;
}

// ---- MAX30205 (body temperature) ----
static int g_max30205_addr = -1;

static bool max30205_begin() {
    if (i2c_ping(MAX30205_ADDR)) 
    { 
        g_max30205_addr = MAX30205_ADDR; 
    }
    if (g_max30205_addr < 0) {
        Serial.println("MAX30205: not found at 0x48..0x4B");
        return false;
    }
    Serial.printf("MAX30205: found at 0x%02X\n", (uint8_t)g_max30205_addr);
    return true; // default mode is continuous conversion
}

static bool max30205_readTemp(float &celsius) {
    if (g_max30205_addr < 0) return false;
    uint8_t buf[2];
    if (i2c_readN((uint8_t)g_max30205_addr, 0x00, buf, 2) != 2) return false;
    int16_t raw = (int16_t)((buf[0] << 8) | buf[1]); // MSB first
    celsius = raw / 256.0f; // 1 LSB = 1/256°C
    return true;
}

// --- Simple heart-rate estimation state (IR-based) ---
const byte HR_RATE_SIZE = 4;
byte  hrRates[HR_RATE_SIZE] = {0};
byte  hrRateSpot = 0;
long  hrLastBeat = 0;      // ms timestamp of last beat
float hrBpm = 0.0f;
int   hrBpmAvg = 0;

// Beat interval buffer for median smoothing
static const uint8_t BEAT_INT_SIZE = 8; // up to last 8 intervals
uint16_t beatIntervals[BEAT_INT_SIZE] = {0};
uint8_t  beatIntCount = 0;

// Helper: median of current intervals
static float medianIntervalMs() {
  if (beatIntCount < 3) return 0.0f; // need a few beats first
  uint16_t temp[BEAT_INT_SIZE];
  for (uint8_t i = 0; i < beatIntCount; ++i) temp[i] = beatIntervals[i];
  // Insertion sort (small n)
  for (uint8_t i = 1; i < beatIntCount; ++i) {
    uint16_t v = temp[i];
    int j = i - 1;
    while (j >= 0 && temp[j] > v) { temp[j + 1] = temp[j]; --j; }
    temp[j + 1] = v;
  }
  if (beatIntCount & 1) {
    return (float)temp[beatIntCount / 2];
  } else {
    uint16_t a = temp[(beatIntCount / 2) - 1];
    uint16_t b = temp[beatIntCount / 2];
    return (a + b) * 0.5f;
  }
}

// Very simple beat detection: threshold + rising edge
// Adaptive beat detection: removes DC, applies simple filtering and dynamic threshold
static bool detectBeat(long ir)
{
  // DC removal (exponential moving average)
  static float dcMean = 0.0f;
  const float alphaDC = 0.97f; // slower baseline tracking to prevent drift
  dcMean = alphaDC * dcMean + (1.0f - alphaDC) * ir;
  float ac = ir - dcMean;

  // High-pass (difference) then low-pass smoothing
  static float prevAc = 0.0f;
  float hp = ac - prevAc;
  prevAc = ac;
  static float lp = 0.0f;
  lp = 0.85f * lp + 0.15f * hp;

  // Dynamic amplitude tracking
  static float dynThresh = 0.0f;
  dynThresh = 0.995f * dynThresh + 0.005f * fabs(lp);

  // Peak qualification factor reduces double-counting small excursions
  const float PEAK_FACTOR = 1.25f; // raise if still double-counting

  // Refractory period (min interval) 450ms (~133 BPM max displayed)
  static uint32_t lastPeakMs = 0;
  uint32_t now = millis();
  const uint32_t REFRACTORY_MS = 450;

  static float prevLp = 0.0f;
  bool risingCross = (lp > dynThresh * PEAK_FACTOR) && (prevLp <= dynThresh * PEAK_FACTOR);
  bool beat = false;
  if (risingCross && (now - lastPeakMs) > REFRACTORY_MS) {
    lastPeakMs = now;
    beat = true;
  }
  prevLp = lp;
  return beat;
}

// ---- Sketch ----
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\nSensors demo (BMI270 + MAX30102 + MAX30205)");

    // Init I2C on pins from app_config.h
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);
    delay(10);

    // Bring sensors online
    (void)bmi270_begin();
    (void)max30102_begin();
    (void)max30205_begin();
}

void loop() {
  // IMU sample accumulate
  static double axSum=0, aySum=0, azSum=0, gxSum=0, gySum=0, gzSum=0, tSumF=0; 
  static uint32_t imuCount = 0;
  ImuSample imu = bmi270_read();
  if (imu.ok) {
    axSum += imu.ax_g;
    aySum += imu.ay_g;
    azSum += imu.az_g;
    gxSum += imu.gx_dps;
    gySum += imu.gy_dps;
    gzSum += imu.gz_dps;
    if (!isnan(imu.temp_c)) tSumF += (imu.temp_c * 1.8 + 32.0);
    imuCount++;
  }

  // Body temperature accumulate (use sensor conversion directly for averaging)
  static double bodyTempCSum = 0.0; static double bodyTempFSum = 0.0; static uint32_t bodyTempCount = 0;
  float tCInstant;
  if (max30205_readTemp(tCInstant)) {
    bodyTempCSum += tCInstant;
    bodyTempFSum += (tCInstant * 9.0f / 5.0f + 32.0f);
    bodyTempCount++;
  }

  // Heart-rate photoplethysmography (raw RED/IR + BPM estimate; HR smoothing handled separately)
    PpgSample ppg = max30102_readOne();
    if (ppg.ok) {
        long irValue = (long)ppg.ir;

        // Beat detection (adaptive)
        if (detectBeat(irValue)) {
            long now = millis();
            long delta = now - hrLastBeat;
            hrLastBeat = now;

            if (delta > 0) {
                // Store interval for median smoothing
                if (delta < 3000) { // ignore unrealistically long gaps
                  // shift if full
                  if (beatIntCount < BEAT_INT_SIZE) {
                    beatIntervals[beatIntCount++] = (uint16_t)delta;
                  } else {
                    // rotate left (drop oldest)
                    for (uint8_t i = 1; i < BEAT_INT_SIZE; ++i) beatIntervals[i-1] = beatIntervals[i];
                    beatIntervals[BEAT_INT_SIZE-1] = (uint16_t)delta;
                  }
                }
                float med = medianIntervalMs();
                if (med > 300.0f && med < 1500.0f) { // 40–200 BPM plausible window
                  hrBpm = 60000.0f / med;
                }
                // Simple rolling byte average for display smoothing
                if (hrBpm > 30.0f && hrBpm < 200.0f) {
                  hrRates[hrRateSpot++] = (byte)hrBpm;
                  hrRateSpot %= HR_RATE_SIZE;
                  hrBpmAvg = 0;
                  for (byte i = 0; i < HR_RATE_SIZE; ++i) hrBpmAvg += hrRates[i];
                  hrBpmAvg /= HR_RATE_SIZE;
                }
            }
        }
    } 
    else 
    {
        Serial.println("PPG no new sample");
    }

    // Buffered / gated output every ~500ms
    static uint32_t lastPrintMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastPrintMs >= 500) {
      lastPrintMs = nowMs;
      // Compute IMU averages
      double axAvg = imuCount ? axSum / imuCount : NAN;
      double ayAvg = imuCount ? aySum / imuCount : NAN;
      double azAvg = imuCount ? azSum / imuCount : NAN;
      double gxAvg = imuCount ? gxSum / imuCount : NAN;
      double gyAvg = imuCount ? gySum / imuCount : NAN;
      double gzAvg = imuCount ? gzSum / imuCount : NAN;
      double tFAvg = (imuCount && tSumF>0) ? tSumF / imuCount : NAN;
      // Compute body temp averages
      double bodyTCAvg = bodyTempCount ? bodyTempCSum / bodyTempCount : NAN;
      double bodyTFAvg = bodyTempCount ? bodyTempFSum / bodyTempCount : NAN;

      Serial.printf("IMU(avg) a[g]=[% .3f % .3f % .3f] g[dps]=[% .2f % .2f % .2f]",
                    axAvg, ayAvg, azAvg, gxAvg, gyAvg, gzAvg);
      if (!isnan(tFAvg)) Serial.printf(" t=%.1fF", tFAvg);
      Serial.print("\n");
      Serial.printf("Heart BPM=%.1f AvgBPM=%d beats=%u\n", hrBpm, hrBpmAvg, (unsigned)beatIntCount);
      if (!isnan(bodyTCAvg)) {
        Serial.printf("Body Temp(avg): %.2f C (%.2f F)\n", bodyTCAvg, bodyTFAvg);
      } else {
        Serial.println("Body Temp: no samples");
      }
      Serial.println("---");
      // reset accumulators
      axSum=aySum=azSum=gxSum=gySum=gzSum=tSumF=0; imuCount=0;
      bodyTempCSum=bodyTempFSum=0; bodyTempCount=0;
    }
    // Small processing delay (sensor ~100Hz, we drain FIFO)
    delay(10);
}
