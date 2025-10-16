#include <Arduino.h>
#include <Wire.h>

static constexpr int SDA_PIN = 21;
static constexpr int SCL_PIN = 22;
static constexpr uint8_t BMI270_ADDR = 0x68; // or 0x69 if SDO pulled high

static uint8_t i2cRead8(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF; // repeated start
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}

static void scanAt(uint32_t hz) {
  // Re-init bus at the requested speed
  Wire.end();
  delay(2);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(hz);
  delay(10);

  Serial.printf("\nScanning I2C at %lu Hz...\n", hz);
  uint8_t found = 0;

  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("  Device found at 0x%02X\n", addr);
      found++;
    } else if (err != 2) {
      // 2 = received NACK on transmit of address (normal for empty slots)
      Serial.printf("  0x%02X responded with I2C error %u\n", addr, err);
    }
  }
  Serial.printf("Found %u device(s)\n", found);

  // Optional sanity: try known WHOAMI if BMI270 present
  if (found > 0) {
    // BMI270 CHIP_ID is at register 0x00, typically 0x24
    uint8_t chip = i2cRead8(BMI270_ADDR, 0x00);
    if (chip != 0xFF) {
      Serial.printf("BMI270 @0x%02X CHIP_ID = 0x%02X (expect ~0x24)\n", BMI270_ADDR, chip);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nI2C multi-speed scan (ESP32 DevKit V1)");
  scanAt(100000);   // 100 kHz first
  scanAt(400000);   // then try 400 kHz
}

void loop() {
  // no-op
}
