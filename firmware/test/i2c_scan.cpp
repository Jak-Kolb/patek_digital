#include <Arduino.h>
#include <Wire.h>
#include "app_config.h"

static constexpr uint8_t BMI270_ADDR = 0x68; // or 0x69 if SDO pulled high
static constexpr uint8_t BMI270_ADDR_ALT = 0x69; // alternate if SDO high

// Attempt a simple bus recovery if SDA is being held low by a misbehaving device.
// Returns true if recovery performed, false if bus already idle.
static bool i2cBusRecover() {
  int sda = digitalRead(I2C_SDA_PIN);
  int scl = digitalRead(I2C_SCL_PIN);
  if (sda == HIGH) return false; // bus looks free
  Serial.println("SDA stuck LOW — attempting bus recovery toggling SCL");
  Wire.end();
  delay(1);
  pinMode(I2C_SCL_PIN, OUTPUT);
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  // Clock up to 16 pulses to advance any slave state machine
  for (int i = 0; i < 16 && digitalRead(I2C_SDA_PIN) == LOW; ++i) {
    digitalWrite(I2C_SCL_PIN, HIGH);
    delayMicroseconds(50);
    digitalWrite(I2C_SCL_PIN, LOW);
    delayMicroseconds(50);
  }
  // Generate a STOP: SDA goes HIGH while SCL HIGH
  digitalWrite(I2C_SCL_PIN, HIGH);
  delayMicroseconds(50);
  // Release SDA (already input with pull-up)
  delayMicroseconds(50);
  int sda_after = digitalRead(I2C_SDA_PIN);
  Serial.printf("Bus recovery complete. SDA=%s\n", sda_after ? "HIGH" : "LOW");
  // Re-init Wire
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000); // start slow after recovery
  delay(2);
  return true;
}

static uint8_t i2cRead8(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF; // repeated start
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}

// Poll BMI270 interrupt pin for a short duration and print level changes.
static void pollBmiInt(uint32_t duration_ms) {
#if defined(BMI270_INT_PIN) && (BMI270_INT_PIN >= 0)
  pinMode(BMI270_INT_PIN, INPUT); // GPIO34 has no internal pull-ups; ensure external pull-up if INT is open-drain
  int last = digitalRead(BMI270_INT_PIN);
  uint32_t start = millis();
  Serial.printf("Polling BMI INT on GPIO%d for %lu ms. Start=%s\n", BMI270_INT_PIN, duration_ms, last ? "HIGH" : "LOW");
  while ((millis() - start) < duration_ms) {
    int lvl = digitalRead(BMI270_INT_PIN);
    if (lvl != last) {
      Serial.printf("  INT change -> %s at %lu ms\n", lvl ? "HIGH" : "LOW", (unsigned long)(millis() - start));
      last = lvl;
    }
    delay(1);
  }
  Serial.println("Done polling INT.");
#else
  (void)duration_ms;
#endif
}

static void scanAt(uint32_t hz) {
  // Re-init bus at the requested speed
  Wire.end();
  delay(2);
  // Enable weak internal pull-ups (still need external pull-ups for reliable 400kHz)
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, INPUT_PULLUP);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(hz);
  delay(10);

  Serial.printf("\nScanning I2C at %lu Hz on SDA=%d SCL=%d...\n", hz, I2C_SDA_PIN, I2C_SCL_PIN);
  int sda_level = digitalRead(I2C_SDA_PIN);
  int scl_level = digitalRead(I2C_SCL_PIN);
  Serial.printf("Line levels before scan: SDA=%s, SCL=%s (expect HIGH/HIGH)\n",
                sda_level ? "HIGH" : "LOW", scl_level ? "HIGH" : "LOW");
  if (sda_level == LOW) {
    i2cBusRecover();
    sda_level = digitalRead(I2C_SDA_PIN);
    scl_level = digitalRead(I2C_SCL_PIN);
  }
  while (sda_level == LOW || scl_level == LOW) {
    Serial.printf("Bus still stuck: SDA=%s, SCL=%s\n",
                  sda_level ? "HIGH" : "LOW", scl_level ? "HIGH" : "LOW");
    delay(100);
    i2cBusRecover();
    sda_level = digitalRead(I2C_SDA_PIN);
    scl_level = digitalRead(I2C_SCL_PIN);
  } 
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

  // Try BMI270 at both possible addresses regardless (helps diagnose strap state)
  for (uint8_t bmiAddr : {BMI270_ADDR, BMI270_ADDR_ALT}) {
    Wire.beginTransmission(bmiAddr);
    uint8_t txErr = Wire.endTransmission();
    if (txErr == 0) {
      uint8_t chip = i2cRead8(bmiAddr, 0x00); // CHIP_ID register
      if (chip == 0xFF) {
        Serial.printf("BMI270 @0x%02X ACKed, but read failed (chip=0xFF).\n", bmiAddr);
      } else {
        Serial.printf("BMI270 candidate @0x%02X CHIP_ID=0x%02X (expect ~0x24).\n", bmiAddr, chip);
      }
    } else if (txErr != 2) {
      Serial.printf("BMI270 probe @0x%02X got I2C error %u (not simple NACK).\n", bmiAddr, txErr);
    } else {
      Serial.printf("BMI270 not responding at 0x%02X (NACK).\n", bmiAddr);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\nI2C multi-speed scan (pins from app_config.h)");
  while(1) {
    scanAt(100000);   // 100 kHz first
    // Briefly poll BMI INT line to observe activity/level changes
    // pollBmiInt(500);
    scanAt(400000);   // then try 400 kHz
    // pollBmiInt(500);
    delay(200);
  }
}

void loop() {
  // no-op
}
