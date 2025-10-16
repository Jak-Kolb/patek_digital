#include <Arduino.h>
#include <Wire.h>
#include "SparkFun_BMI270_Arduino_Library.h"
#include "MAX30105.h"
#include "heartRate.h"
#include <SparkFun_MAX30205.h>

static constexpr int SDA_PIN = 21;
static constexpr int SCL_PIN = 22;

// --- TLV helpers for 256B frame ---
static void tlv_put(uint8_t*& p, uint8_t type, const void* data, uint8_t len) {
  *p++ = type;
  *p++ = len;
  memcpy(p, data, len);
  p += len;
}

static void dump_hex_256(const uint8_t* buf) {
  for (int i = 0; i < 256; ++i) {
    if ((i % 16) == 0) Serial.printf("\n%03d: ", i);
    Serial.printf("%02X ", buf[i]);
  }
  Serial.println();
}

// --- Sensors ---
BMI270 imu;
MAX30105 ppg;
MAX30205 tempSensor;

bool haveIMU = false, havePPG = false, haveTMP = false;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[ SUB1 PACK DEMO: BMI270 + MAX30102 + MAX30205 ]");

  // I2C up
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  delay(10);

  // BMI270
  if (imu.beginI2C(0x68, Wire) == BMI2_OK) {
    haveIMU = true;
    imu.setAccelODR(BMI2_ACC_ODR_100HZ);
    imu.setGyroODR(BMI2_GYR_ODR_100HZ);
    Serial.println("BMI270 OK @0x68");
  } else {
    Serial.println("BMI270 NOT FOUND");
  }

  // MAX30102
  if (ppg.begin(Wire, I2C_SPEED_FAST)) {
    havePPG = true;
    ppg.setup();
    ppg.setPulseAmplitudeRed(0x0A);
    ppg.setPulseAmplitudeIR(0x24);
    ppg.setPulseAmplitudeGreen(0x00);
    ppg.setSampleRate(100);
    Serial.println("MAX30102 OK @0x57");
  } else {
    Serial.println("MAX30102 NOT FOUND");
  }

  // MAX30205
  if (tempSensor.begin(0x48)) {
    haveTMP = true;
    tempSensor.shutdown(false);
    tempSensor.setContinuous();
    Serial.println("MAX30205 OK @0x48");
  } else {
    Serial.println("MAX30205 NOT FOUND");
  }
}

void loop() {
  // --- Read sensors (best-effort) ---
  // BMI270
  int16_t ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
  if (haveIMU) {
    if (imu.getSensorData() == BMI2_OK) {
      // Convert g/deg/s floats to milli-units int16 for compactness
      ax = (int16_t)roundf(imu.data.accelX * 1000.0f);
      ay = (int16_t)roundf(imu.data.accelY * 1000.0f);
      az = (int16_t)roundf(imu.data.accelZ * 1000.0f);
      gx = (int16_t)roundf(imu.data.gyroX  * 10.0f);    // deg/s *10
      gy = (int16_t)roundf(imu.data.gyroY  * 10.0f);
      gz = (int16_t)roundf(imu.data.gyroZ  * 10.0f);
    } else {
      Serial.println("[warn] BMI270 read failed");
    }
  }

  // MAX30102
  uint32_t ir = 0, red = 0;
  uint16_t bpm_x10 = 0;
  static long lastBeat = 0;
  static const byte RATE_SIZE = 4;
  static byte rates[RATE_SIZE];
  static byte rateSpot = 0;

  if (havePPG) {
    ir  = (uint32_t)ppg.getIR();
    red = (uint32_t)ppg.getRed();
    if (checkForBeat((long)ir)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      float bpm = 60.0f / (delta / 1000.0f);
      if (bpm >= 20 && bpm <= 255) {
        rates[rateSpot++] = (byte)bpm;
        rateSpot %= RATE_SIZE;
        bpm_x10 = (uint16_t)roundf(bpm * 10.0f);
      }
    }
  }

  // MAX30205
  int16_t temp_c_x100 = 0;
  if (haveTMP) {
    float c = tempSensor.readTemperature();
    temp_c_x100 = (int16_t)roundf(c * 100.0f);
  }

  // --- Build 256B frame ---
  uint8_t frame[256];
  memset(frame, 0xEE, sizeof(frame));   // pad
  uint8_t* p = frame;

  // Header (8 bytes): "SB2\0", version=1, 3x reserved, uptime (u32)
  const uint8_t magic[4] = {'S','B','2',0x00};
  memcpy(p, magic, 4); p += 4;
  *p++ = 0x01;            // version
  *p++ = 0x00;            // reserved
  *p++ = 0x00;            // reserved
  *p++ = 0x00;            // reserved
  uint32_t t = millis();
  memcpy(p, &t, 4); p += 4;

  // TLV 0x01: BMI270 accel/gyro (6 * i16 = 12 bytes)
  if (haveIMU) {
    int16_t imu12[6] = {ax, ay, az, gx, gy, gz};
    tlv_put(p, 0x01, imu12, sizeof(imu12));
  }

  // TLV 0x02: MAX30102 IR/RED/BPMx10 (u32,u32,u16 = 10 bytes)
  if (havePPG) {
    struct __attribute__((packed)) {
      uint32_t ir;
      uint32_t red;
      uint16_t bpm_x10;
    } ppg10 { ir, red, bpm_x10 };
    tlv_put(p, 0x02, &ppg10, sizeof(ppg10));
  }

  // TLV 0x03: MAX30205 temp (i16 in 0.01C)
  if (haveTMP) {
    tlv_put(p, 0x03, &temp_c_x100, sizeof(temp_c_x100));
  }

  // Footer TLV 0xFE: simple CRC16 over bytes [0 .. p-1] (optional but nice)
  auto crc16 = [](const uint8_t* d, size_t n) -> uint16_t {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
      c ^= (uint16_t)d[i];
      for (int b = 0; b < 8; ++b)
        c = (c & 1) ? (c >> 1) ^ 0xA001 : (c >> 1);
    }
    return c;
  };
  uint16_t csum = crc16(frame, (size_t)(p - frame));
  tlv_put(p, 0xFE, &csum, sizeof(csum));

  // Zero the remainder instead of 0xEE if you prefer:
  // memset(p, 0x00, frame + sizeof(frame) - p);

  // --- Emit summary + hex (first few lines) ---
  Serial.printf("Frame bytes used: %d / 256  (IMU:%d PPG:%d TMP:%d)\n",
                (int)(p - frame),
                haveIMU, havePPG, haveTMP);

  dump_hex_256(frame);
  Serial.println("----");
  delay(250);
}
