#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

// Current Monitor Demo
// Goal: Measure current draw of the 3.3V rail using an INA219 high-side current sensor
// Wiring (typical):
//   3.3V source -> INA219 VIN+ ; INA219 VIN- -> target 3.3V rail
//   Connect INA219 SDA/SCL to the same I2C bus (default addr 0x40)
//   GND common between INA219 and target
// Notes:
//   - If you want to measure the entire board's consumption, the 3.3V path must pass through the INA219.
//     On many dev boards this requires cutting a trace or powering the system via the INA219 instead of onboard regulator.
//   - To measure only external sensors powered from the 3.3V pin, place INA219 in series with that pin feeding the sensors.

#ifndef BATTERY_MAH
#define BATTERY_MAH 300.0f // Override via build_flags or adjust here
#endif

static Adafruit_INA219 ina219; // default 0x40

static const uint32_t SAMPLE_PERIOD_MS = 100; // 10 Hz sampling
static const uint32_t REPORT_PERIOD_MS = 10000; // 10 s rolling report

// Stats over the report window
static float sum_mA = 0.0f;
static uint32_t count_samples = 0;
static float min_mA = 1e9f;
static float max_mA = -1e9f;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[ Current Monitor Demo - INA219 ]");

  Wire.begin();
  if (!ina219.begin()) {
    Serial.println("INA219 not found at 0x40. Check wiring.");
    while (1) { delay(1000); }
  }

  // Calibrate for expected current range (default is fine for ~3.2A max, 0.1 ohm shunt)
  // For higher precision at lower currents, consider ina219.setCalibration_32V_1A() or custom calibration.
  ina219.setCalibration_32V_1A();

  Serial.printf("Battery capacity (configurable): %.0f mAh\n", BATTERY_MAH);
  Serial.println("Columns: Vbus[V]  I[mA]  P[mW]  avgI[mA](10s)  min/max[mA](10s)  estLife[hr]");
}

void loop() {
  static uint32_t lastSample = 0;
  static uint32_t lastReport = 0;
  uint32_t now = millis();

  if (now - lastSample >= SAMPLE_PERIOD_MS) {
    lastSample = now;

    float busV = ina219.getBusVoltage_V();      // Voltage on V- (load side) in Volts
    float current_mA = ina219.getCurrent_mA();  // Current in mA
    float power_mW = ina219.getPower_mW();      // Power in mW

    // Accumulate stats
    sum_mA += current_mA;
    count_samples++;
    if (current_mA < min_mA) min_mA = current_mA;
    if (current_mA > max_mA) max_mA = current_mA;

    // Print instantaneous sample at lower rate to keep logs readable
    Serial.printf("%.3f  %.1f  %.1f\n", busV, current_mA, power_mW);
  }

  if (now - lastReport >= REPORT_PERIOD_MS && count_samples > 0) {
    lastReport = now;
    float avg_mA = sum_mA / (float)count_samples;
    float est_hours = (avg_mA > 0.1f) ? (BATTERY_MAH / avg_mA) : 0.0f;
    Serial.printf("[10s] avg=%.1f mA  min=%.1f  max=%.1f  est life=%.1f hr (%.0f mAh)\n",
                  avg_mA, min_mA, max_mA, est_hours, BATTERY_MAH);
    // Reset window
    sum_mA = 0.0f; count_samples = 0; min_mA = 1e9f; max_mA = -1e9f;
  }
}
