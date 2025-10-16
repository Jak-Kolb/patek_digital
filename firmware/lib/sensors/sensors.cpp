#include <Arduino.h>
#include <Wire.h>
#include "app_config.h"
#include "sensors.h"

// ---- I2C helpers ----
static inline bool i2c_write_u8(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static inline bool i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    size_t got = Wire.requestFrom((int)addr, (int)n);
    if (got != n) return false;
    for (size_t i = 0; i < n; ++i) buf[i] = Wire.read();
    return true;
}

// ---- MAX30102 (light bring-up) ----
static bool max30102_init() {
    // Reset
    i2c_write_u8(I2C_ADDR_MAX30102, 0x09, 0x40);
    delay(10);
    // Exit shutdown
    i2c_write_u8(I2C_ADDR_MAX30102, 0x09, 0x00);
    // SpO2 config (range/sample rate/pulse width) – tune later
    i2c_write_u8(I2C_ADDR_MAX30102, 0x06, 0x27);
    // Mode: RED + IR
    i2c_write_u8(I2C_ADDR_MAX30102, 0x09, 0x03);
    // LED currents (modest)
    i2c_write_u8(I2C_ADDR_MAX30102, 0x0C, 0x24);
    i2c_write_u8(I2C_ADDR_MAX30102, 0x0D, 0x24);
    return true;
}

static bool max30102_read(uint32_t &ppg) {
    // Read 3 bytes (IR channel example) from FIFO data register (0x07)
    uint8_t b[3];
    if (!i2c_read_bytes(I2C_ADDR_MAX30102, 0x07, b, 3)) return false;
    ppg = ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2];
    ppg &= 0x3FFFF; // 18 bits valid
    return true;
}

// ---- MAX30205 (temperature) ----
static bool max30205_init() {
    // Defaults are continuous-conversion; no config needed for bring-up
    return true;
}

static bool max30205_read(float &tc) {
    uint8_t b[2];
    if (!i2c_read_bytes(I2C_ADDR_MAX30205, 0x00, b, 2)) return false;
    int16_t raw = (int16_t)((b[0] << 8) | b[1]);
    tc = raw * 0.00390625f; // 1 LSB = 0.00390625 °C
    return true;
}

// ---- BMI270 (accel only; minimal) ----
static bool bmi270_init() {
    // Soft reset via CMD register (0x7E)
    i2c_write_u8(I2C_ADDR_BMI270, 0x7E, 0xB6);
    delay(5);
    // Proper BMI270 config (ODR/range) can be added later
    return true;
}

static bool bmi270_read_accel(int16_t &ax, int16_t &ay, int16_t &az) {
    uint8_t b[6];
    // Accel data start around 0x12 (LSB/MSB per axis)
    if (!i2c_read_bytes(I2C_ADDR_BMI270, 0x12, b, 6)) return false;
    ax = (int16_t)((b[1] << 8) | b[0]);
    ay = (int16_t)((b[3] << 8) | b[2]);
    az = (int16_t)((b[5] << 8) | b[4]);
    return true;
}

// ---- Public API ----
void sensors_init() {
    max30102_init();
    max30205_init();
    bmi270_init();
}

bool read_ppg(uint32_t &ppg) { return max30102_read(ppg); }
bool read_temp(float &tc) { return max30205_read(tc); }
bool read_accel(int16_t &ax, int16_t &ay, int16_t &az) { return bmi270_read_accel(ax, ay, az); }

bool poll_all(Sub1Sample &out) {
    bool ok = false;
    out.ts_ms = millis();

    uint32_t ppg;
    if (read_ppg(ppg)) { out.ppg_raw = ppg; ok = true; }

    float tc;
    if (read_temp(tc)) { out.temp_c = tc; ok = true; }

    int16_t ax, ay, az;
    if (read_accel(ax, ay, az)) { out.ax = ax; out.ay = ay; out.az = az; ok = true; }

    return ok;
}
