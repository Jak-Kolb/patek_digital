#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_BMI270_Arduino_Library.h>

BMI270 imu;

void setup() {
  Serial.begin(115200);
  delay(200);

  // ESP32 DevKit V1 I2C pins
  Wire.begin(21, 22);
  Wire.setClock(400000);

  Serial.println("Initializing BMI270 (I2C @ 0x68)...");
  // Change to 0x69 if your SDO pin is tied high
  if (imu.beginI2C(0x68, Wire) != BMI2_OK) {
    Serial.println("BMI270 not found. Check wiring/address.");
    while (true) delay(100);
  }

  // Set output data rates (adjust as needed)
  imu.setAccelODR(BMI2_ACC_ODR_100HZ);
  imu.setGyroODR(BMI2_GYR_ODR_100HZ);

  // (Optional) filter / power settings if you want:
  // imu.setAccelFilterBandwidth(BMI2_ACC_NORMAL_AVG4);
  // imu.setGyroFilterBandwidth(BMI2_GYR_BW_OSR4);
  // imu.setAccelPowerMode(BMI2_ACC_NORMAL);          // see bmi270.h for enums
  // imu.setGyroPowerMode(BMI2_GYR_NORMAL, BMI2_GYR_NOISE_PERF_MODE);

  Serial.println("BMI270 initialized!");
}

void loop() {
  if (imu.getSensorData() == BMI2_OK) {
    const auto& d = imu.data; // BMI270_SensorData with floats

    Serial.printf("A[g]: %+6.3f %+6.3f %+6.3f | G[dps]: %+7.2f %+7.2f %+7.2f | t(ms): %lu\n",
                  d.accelX, d.accelY, d.accelZ,
                  d.gyroX,  d.gyroY,  d.gyroZ,
                  (unsigned long)d.sensorTimeMillis);
  } else {
    Serial.println("Read failed");
  }
  delay(200);
}
