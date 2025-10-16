#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

MAX30105 particleSensor;

// Configuration
const bool PLOT_MODE = false; // set true to emit CSV IR_filtered for plotting
const byte RATE_SIZE = 6;  // larger averaging window for smoother BPM
byte rates[RATE_SIZE];     //Array of heart rates
byte rateSpot = 0;
byte beatCount = 0; // how many valid beats stored (<= RATE_SIZE)
long lastBeat = 0;  // Time at which the last beat occurred

float beatsPerMinute = 0.0f;
int beatAvg = 0;

// Moving-average filter for IR to reduce noise/sub-peaks
const int IR_SMOOTH = 5; // 5-sample moving average
unsigned long ir_window[IR_SMOOTH];
int ir_idx = 0;
bool ir_filled = false;

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing...");

  // Initialize sensor (ESP32: ensure Wire initialized externally or let library start it)
  Wire.begin();
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 was not found. Please check wiring/power.");
    while (1) delay(50);
  }
  Serial.println("Place a steady finger on the sensor (use a rubber band or tape if possible).");

  // Use defaults but ensure red LED is on low to avoid saturation
  particleSensor.setup();                     // Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A);  // Turn Red LED low
  particleSensor.setPulseAmplitudeGreen(0);   // Turn off Green LED
}

void loop() {
  // Wait for a new sample and use the library's check/available/nextSample flow
  while (particleSensor.available() == false) particleSensor.check();
  long raw_ir = particleSensor.getIR();
  particleSensor.nextSample();

  // rolling window average
  ir_window[ir_idx++] = raw_ir;
  if (ir_idx >= IR_SMOOTH) ir_idx = 0;
  if (!ir_filled && ir_idx == 0) ir_filled = true;
  unsigned long ir_sum = 0;
  int ir_count = ir_filled ? IR_SMOOTH : ir_idx;
  for (int i = 0; i < ir_count; ++i) ir_sum += ir_window[i];
  long irValue = (ir_count > 0) ? (long)(ir_sum / ir_count) : raw_ir;

  // If plot mode is enabled emit single-column CSV (one filtered IR per line)
  if (PLOT_MODE) {
    Serial.println(irValue);
    return; // skip human-readable prints when in plot mode
  }

  // Use the raw sample for beat detection to avoid smoothing blunting peaks
  if (checkForBeat(raw_ir) == true) {
    //We sensed a beat!
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute;  // Store this reading in the array
      if (rateSpot >= RATE_SIZE) rateSpot = 0;
      if (beatCount < RATE_SIZE) beatCount++;

      // Take median of stored readings for robust smoothing
      int tmp[RATE_SIZE];
      int start = (rateSpot - beatCount + RATE_SIZE) % RATE_SIZE;
      for (int i = 0; i < beatCount; ++i) tmp[i] = rates[(start + i) % RATE_SIZE];
      // simple sort
      for (int i = 0; i < beatCount - 1; ++i) for (int j = i+1; j < beatCount; ++j) if (tmp[j] < tmp[i]) { int t=tmp[i]; tmp[i]=tmp[j]; tmp[j]=t; }
      beatAvg = tmp[beatCount/2];
    }
  }

  Serial.print("IR="); Serial.print(irValue);
  Serial.print(", BPM="); Serial.print(beatsPerMinute);
  Serial.print(", Med BPM="); Serial.print(beatAvg);
  if (irValue < 50000) Serial.print(" No finger?");
  Serial.println();
}