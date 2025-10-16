// src/aht20_demo.cpp
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>

Adafruit_AHTX0 aht;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  if (!aht.begin()) {
    Serial.println("AHT20 not found");
    while (1) delay(10);
  }
  Serial.println("AHT20 ready");
}

void loop() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);
  Serial.printf("Temp: %.2f C (%.2f F), Humidity: %.2f%%\n", temp.temperature, temp.temperature*9/5+32, humidity.relative_humidity);
  delay(1000);
}
