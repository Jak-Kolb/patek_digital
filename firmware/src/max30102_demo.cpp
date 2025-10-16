#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"     // SparkFun MAX3010x lib
#include "heartRate.h"    // comes with the SparkFun library

static constexpr int SDA_PIN = 21;
static constexpr int SCL_PIN = 22;

MAX30105 ppg;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[ MAX30102 DEMO - IR Amplitude Sweep ]");

  // Bring up I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000); // MAX30102 tolerates fast mode
  delay(10);

  // Try to init (use default I2C @ 0x57)
  if (!ppg.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found at 0x57. Check wiring/power.");
    return;
  }
  Serial.println("MAX30102 found.");

  // Minimal, stable config
  ppg.setup(); // default config in SparkFun lib
  ppg.setPulseAmplitudeRed(0x0A);
  ppg.setPulseAmplitudeGreen(0x00);
  ppg.setSampleRate(100);
  // Use SparkFun library constants to avoid warnings and ensure correct config:
  // MAX30105_PULSEWIDTH_411 = 0x03, MAX30105_ADCRANGE_16384 = 0x60
  ppg.setPulseWidth(0x03);   // 411us, 18-bit resolution
  ppg.setADCRange(0x60);     // 16384, largest ADC range
}

void loop() {
  static uint8_t ir_amp = 0x05;
  static unsigned long lastSweep = 0;
  static unsigned long sweepInterval = 3000; // 3 seconds per amplitude
  static bool sweepDone = false;

  // Sweep IR amplitude from 0x05 to 0xFF
  if (!sweepDone && millis() - lastSweep > sweepInterval) {
    ir_amp += 0x10;
    if (ir_amp > 0xFF) {
      ir_amp = 0xFF;
      sweepDone = true;
      Serial.println("[Sweep] Finished IR amplitude sweep. Leave finger on sensor and observe IR/BPM.");
    } else {
      ppg.setPulseAmplitudeIR(ir_amp);
      Serial.printf("[Sweep] Set IR amplitude to 0x%02X\n", ir_amp);
    }
    lastSweep = millis();
  }

  long ir = ppg.getIR();
  long red = ppg.getRed();

  static long lastBeat = 0;
  static float beatsPerMinute = 0;
  static int beatAvg = 0;
  static const byte RATE_SIZE = 4;
  static byte rates[RATE_SIZE];
  static byte rateSpot = 0;

  bool beat = checkForBeat(ir);
  if (beat) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    beatsPerMinute = 60.0 / (delta / 1000.0);
    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      int sum = 0;
      for (byte i = 0; i < RATE_SIZE; i++) sum += rates[i];
      beatAvg = sum / RATE_SIZE;
    }
    Serial.println("[PPG] Beat detected!");
  }

  Serial.printf("IR=%ld, RED=%ld, BPM=%.1f, Avg=%d, IR_amp=0x%02X%s\n",
                ir, red, beatsPerMinute, beatAvg, ir_amp,
                (ir < 50000 ? "  (No finger?)" : ""));
  delay(50);
}
