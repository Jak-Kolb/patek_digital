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
#include "MAX30105.h"
#include "heartRate.h"
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

// --- MAX30102 (using SparkFun Library) ---
static MAX30105 particleSensor;

const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

static bool max30102_begin() {
  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) { // Use default I2C port, 400kHz speed
    Serial.println("MAX30102: not found");
    return false;
  }

  // Setup with optimal settings for heart rate
  byte ledBrightness = 0x1F; // Options: 0=Off to 255=50mA. 0x1F (approx 6.4mA) is a good starting point
  byte sampleAverage = 4;    // Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 3;          // Options: 1 = Red only, 2 = Red + DC, 3 = Red + IR
  int sampleRate = 100;      // Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;      // Options: 69, 118, 215, 411
  int adcRange = 4096;       // Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(ledBrightness);
  particleSensor.setPulseAmplitudeGreen(0); // Turn off Green LED
  
  Serial.println("MAX30102 ready");
  return true;
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

static TaskHandle_t g_sensorTaskHandle = nullptr;

// --- ISR flags ---
// Removed volatile flags in favor of direct task notifications

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

// --- Heart rate estimation state ---
static void updateHeartRate(long irValue) {
  if (checkForBeat(irValue) == true) {
    //We sensed a beat!
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
      rateSpot %= RATE_SIZE; //Wrap variable

      //Take average of readings
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }
}

static const uint32_t EVT_IMU  = (1 << 0);
static const uint32_t EVT_PPG  = (1 << 1);
static const uint32_t EVT_TEMP = (1 << 2);

void IRAM_ATTR onImuTimer()  { 
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (g_sensorTaskHandle) xTaskNotifyFromISR(g_sensorTaskHandle, EVT_IMU, eSetBits, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}
void IRAM_ATTR onPpgTimer()  { 
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (g_sensorTaskHandle) xTaskNotifyFromISR(g_sensorTaskHandle, EVT_PPG, eSetBits, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}
void IRAM_ATTR onTempTimer() { 
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (g_sensorTaskHandle) xTaskNotifyFromISR(g_sensorTaskHandle, EVT_TEMP, eSetBits, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

static hw_timer_t* setupTimer(uint8_t id, uint16_t divider, uint64_t periodUs, void (*isr)()) {
  hw_timer_t* t = timerBegin(id, divider, true);
  timerAttachInterrupt(t, isr, true);
  timerAlarmWrite(t, periodUs, true);
  timerAlarmEnable(t);
  return t;
}

static void sensorsTask(void* arg);

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
  tPpg  = setupTimer(1, 80, 10000,    onPpgTimer);   // 100 Hz
  tTemp = setupTimer(2, 80, 1000000,  onTempTimer);  // 1 Hz
  halfWindowStartMs = millis();

  xTaskCreatePinnedToCore(sensorsTask, "Sensors", 4096, NULL, 2, &g_sensorTaskHandle, 1);
}

static float lastBodyTempC = 0.0f;

// --- HR Median Buffer ---
static int hrBuffer[4] = {0};
static uint8_t hrBufferIdx = 0;

static void pushHrValue(int val) {
    hrBuffer[hrBufferIdx] = val;
    hrBufferIdx = (hrBufferIdx + 1) % 4;
}

static int getMedianHr() {
    int sorted[4];
    memcpy(sorted, hrBuffer, sizeof(sorted));
    // Simple bubble sort for 4 elements
    for(int i=0; i<3; i++) {
        for(int j=i+1; j<4; j++) {
            if(sorted[j] < sorted[i]) {
                int t = sorted[i]; sorted[i] = sorted[j]; sorted[j] = t;
            }
        }
    }
    // Median of 4: average of index 1 and 2
    return (sorted[1] + sorted[2]) / 2;
}

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
    rs.hr_bpm = (reg_buffer::float16)getMedianHr();
    rs.temp_c = (reg_buffer::float16)lastBodyTempC;
    
    const time_t t_now = time(nullptr);
    if (t_now > 1000000000) {
        rs.timestamp = (uint32_t)t_now;
    } else {
        rs.timestamp = (uint32_t)(millis() / 1000);
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
  particleSensor.check(); // Check the sensor, read up to 4 samples

  while (particleSensor.available()) {
    uint32_t ir = particleSensor.getFIFOIR();
    uint32_t red = particleSensor.getFIFORed();
    
    redSum += red; 
    irSum += ir; 
    ppgCount++;
    
    updateHeartRate(ir); // updates beatAvg internally
    pushHrValue(beatAvg);
    
    particleSensor.nextSample(); // Move to next sample
  }
}

static void sampleTemp() {
  float c; if (!max30205_readTemp(c)) return; 
  lastBodyTempC = c;
  bodyTempCSum += c; bodyTempFSum += (c * 9.0/5.0 + 32.0); tempCount++; 
}

static void sensorsTask(void* arg) {
  uint32_t events;
  while(true) {
    // Wait for notification bits
    xTaskNotifyWait(0, ULONG_MAX, &events, portMAX_DELAY);

    if (events & EVT_IMU) {
      sampleImu();
    }
    if (events & EVT_PPG) {
      samplePpg();
    }
    if (events & EVT_TEMP) {
      sampleTemp();
      
      // Compute averages (triggered by 1Hz temp timer)
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
      Serial.printf("HR=%d BPM (Avg)\n", beatAvg);
      Serial.printf("HR=%.1f BPM (Recent)\n", beatsPerMinute);
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
  }
}

void sensors_loop() {
  // Empty - logic moved to sensorsTask
}
