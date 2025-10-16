// src/max30205_demo.cpp
#include <Arduino.h>
#include <Wire.h>
#include <MAX30205.h>   // Rob Tillaart library

MAX30205 T; // default I2C addr 0x48

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  if (!T.begin()) {
    Serial.println("MAX30205 not found");
    while (1) delay(10);
  }
  T.setContinuous(true);   // optional
  Serial.println("MAX30205 ready");
}

void loop() {
  float C = T.read();
  Serial.printf("Body temp: %.2f C (%.2f F)\n", C, C*9/5+32);
  delay(500);
}
