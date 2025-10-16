#include <Arduino.h>

const int SDA_PIN = 21;
const int SCL_PIN = 22;

// Clock SCL manually to free a stuck SDA line
void i2cBusRecovery() {
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, OUTPUT);
  digitalWrite(SCL_PIN, HIGH);
  delay(2);

  // If SDA held low, toggle SCL up to 16 times to release it
  for (int i = 0; i < 16 && digitalRead(SDA_PIN) == LOW; ++i) {
    digitalWrite(SCL_PIN, LOW);
    delayMicroseconds(5);
    digitalWrite(SCL_PIN, HIGH);
    delayMicroseconds(5);
  }

  // Generate a STOP: SDA low → high while SCL high
  pinMode(SDA_PIN, OUTPUT);
  digitalWrite(SDA_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(SCL_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(SDA_PIN, HIGH);
  delay(2);

  // Return to safe input state
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, INPUT_PULLUP);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[I2C Bus Recovery]");
  i2cBusRecovery();
  Serial.println("Recovery complete. Try running your I2C scanner again.");
}

void loop() {}
