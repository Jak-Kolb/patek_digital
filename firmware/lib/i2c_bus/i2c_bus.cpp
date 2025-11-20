#include <Wire.h>
#include "app_config.h"
#include "i2c_bus.h"

void i2c_setup() {
    // Explicitly set ESP32 DevKit V1 pins: SDA=21, SCL=22
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_CLOCK_HZ);
}
