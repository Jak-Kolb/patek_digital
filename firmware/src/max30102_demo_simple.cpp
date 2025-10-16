#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <math.h>

MAX30105 particleSensor;

// Tunables for experimentation
// Compile-time toggles to aid troubleshooting
#ifndef HR_DEBUG
#define HR_DEBUG 1
#endif
#ifndef HR_RELAXED
#define HR_RELAXED 1   // relax thresholds to ensure BPM shows up
#endif
#ifndef HR_CLASSIC_MODE
#define HR_CLASSIC_MODE 1  // default to classic SparkFun-style sampling
#endif
#ifndef HR_AUTO_IR
#define HR_AUTO_IR 0       // off by default to keep behavior close to original
#endif

static constexpr uint32_t SERIAL_BAUD = 115200;      // match platformio.ini monitor_speed
static constexpr uint16_t IR_FINGER_THRESHOLD = HR_RELAXED ? 12000 : 50000; // finger presence
static constexpr uint8_t  RATE_SIZE = 8;             // smoothing window (beats)
static constexpr uint32_t PRINT_PERIOD_MS = 200;     // limit prints to ~5 Hz for clarity
static constexpr uint8_t  IR_AMP_MIN = 0x02;
static constexpr uint8_t  IR_AMP_MAX = 0xFF;
static constexpr uint32_t IR_TARGET_LOW = 70000;     // target DC window for auto IR
static constexpr uint32_t IR_TARGET_HIGH = 230000;
static constexpr uint32_t AUTO_IR_PERIOD_MS = 500;
static constexpr float    EMA_ALPHA = 0.2f;          // EMA smoothing for BPM
static constexpr float    MAX_BPM_SLEW = 15.0f;      // max change per beat (BPM)
static constexpr uint16_t REFRACT_MS = HR_RELAXED ? 250 : 300; // refractory period (used only in enhanced path)
static constexpr uint16_t IBI_MIN_MS = 270;          // ~222 BPM
static constexpr uint16_t IBI_MAX_MS = 2000;         // 30 BPM
static constexpr float    MIN_AC_ABS = HR_RELAXED ? 80.0f : 300.0f; // min AC magnitude

uint8_t rates[RATE_SIZE] = {0}; // rolling BPM samples
uint8_t rateSpot = 0;
unsigned long lastBeat = 0;  // ms timestamp of last beat
float beatsPerMinute = 0.0f;
int beatAvg = 0;
unsigned long lastPrint = 0;
uint8_t irAmp = 0x20;             // start moderate, will auto-adjust
unsigned long lastAutoIr = 0;

// Simple DC/AC estimators for quality gating
static float dcEst = 0.0f;
static float acAbsLPF = 0.0f; // LPF of |AC|

// IBI storage for robust median filtering
static const uint8_t IBI_WIN = 7;
static uint16_t ibiBuf[IBI_WIN] = {0};
static uint8_t ibiSpot = 0;
static bool ibiFilled = false;
static float bpmEma = 0.0f;

static uint16_t medianIBI() {
  uint16_t tmp[IBI_WIN];
  uint8_t n = ibiFilled ? IBI_WIN : ibiSpot;
  if (n == 0) return 0;
  for (uint8_t i = 0; i < n; ++i) tmp[i] = ibiBuf[i];
  // insertion sort small array
  for (uint8_t i = 1; i < n; ++i) {
    uint16_t key = tmp[i];
    int j = i - 1;
    while (j >= 0 && tmp[j] > key) { tmp[j+1] = tmp[j]; j--; }
    tmp[j+1] = key;
  }
  return tmp[n/2];
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println("\n[ MAX30102 Simple HR Demo ]");

  // Initialize I2C and sensor
  Wire.begin();
  Wire.setClock(400000);
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 was not found. Please check wiring/power. ");
    while (1)
      ;
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  // Configure sensor
  particleSensor.setup();                     // default config from SparkFun lib (100 sps, etc.)
  particleSensor.setSampleRate(100);
  particleSensor.setPulseWidth(0x03);         // 411us, 18-bit
  particleSensor.setADCRange(0x60);           // 16384 nA range (max FS)
  particleSensor.setPulseAmplitudeIR(irAmp);  // start at moderate IR, auto-adjust below
  particleSensor.setPulseAmplitudeRed(0x0A);  // low red LED just as a running indicator
  particleSensor.setPulseAmplitudeGreen(0x00);
}

void loop() {
  // Ensure FIFO is serviced and new samples are available
  while (particleSensor.available() == false) {
    particleSensor.check();
  }

  long irValue = particleSensor.getIR();

  // Update DC/AC estimators for quality gating
  if (dcEst == 0.0f) dcEst = (float)irValue;
  dcEst += 0.01f * ((float)irValue - dcEst); // slow EMA for DC
  float ac = (float)irValue - dcEst;
  acAbsLPF = 0.9f * acAbsLPF + 0.1f * fabsf(ac);

  // Finger detection gate: when no finger, reset HR state
  bool fingerOn = irValue > IR_FINGER_THRESHOLD;
  if (!fingerOn) {
    beatsPerMinute = 0.0f;
    beatAvg = 0;
    rateSpot = 0;
    lastBeat = millis();
    bpmEma = 0.0f;
    ibiSpot = 0; ibiFilled = false;
  }

  // Beat detection on raw IR
#if HR_CLASSIC_MODE
  if (fingerOn && checkForBeat(irValue)) {
    unsigned long now = millis();
    unsigned long delta = now - lastBeat;
    lastBeat = now;
    float bpm = 60.0f / (delta / 1000.0f);
    if (bpm >= 30.0f && bpm <= 220.0f) {
      beatsPerMinute = bpm;
      rates[rateSpot++] = (uint8_t)lroundf(bpm);
      if (rateSpot >= RATE_SIZE) rateSpot = 0;
      int sum = 0; for (uint8_t i = 0; i < RATE_SIZE; i++) sum += rates[i];
      beatAvg = sum / RATE_SIZE;
      if (HR_DEBUG) {
        Serial.print("[Beat] BPM="); Serial.println(beatsPerMinute, 1);
      }
    }
  }
#else
  bool qualityOK = (acAbsLPF >= MIN_AC_ABS);
  if (fingerOn && (qualityOK || HR_RELAXED) && checkForBeat(irValue)) {
    unsigned long now = millis();
    unsigned long delta = now - lastBeat;
    if (delta < REFRACT_MS) {
      // within refractory period, ignore
    } else if (delta >= IBI_MIN_MS && delta <= IBI_MAX_MS) { // reject outliers
      lastBeat = now;
      float bpm = 60.0f / (delta / 1000.0f);
      if (bpm >= 30.0f && bpm <= 220.0f) {
        ibiBuf[ibiSpot++] = (uint16_t)delta;
        if (ibiSpot >= IBI_WIN) { ibiSpot = 0; ibiFilled = true; }
        uint16_t medIbi = medianIBI();
        if (medIbi > 0) {
          float bpmMed = 60000.0f / (float)medIbi;
          float target = bpmMed;
          if (bpmEma > 0.0f) {
            float diff = target - bpmEma;
            if (diff >  MAX_BPM_SLEW) target = bpmEma + MAX_BPM_SLEW;
            if (diff < -MAX_BPM_SLEW) target = bpmEma - MAX_BPM_SLEW;
            bpmEma = (1.0f - EMA_ALPHA) * bpmEma + EMA_ALPHA * target;
          } else {
            bpmEma = target;
          }
          beatsPerMinute = bpmEma;
          rates[rateSpot++] = (uint8_t)lroundf(beatsPerMinute);
          if (rateSpot >= RATE_SIZE) rateSpot = 0;
          int sum = 0; for (uint8_t i = 0; i < RATE_SIZE; i++) sum += rates[i];
          beatAvg = sum / RATE_SIZE;
        }
      }
    }
  }
#endif

  // Move to next sample in FIFO
  particleSensor.nextSample();

  // Auto IR amplitude control to keep IR DC in target window
  unsigned long nowAuto = millis();
#if HR_AUTO_IR
  if (nowAuto - lastAutoIr >= AUTO_IR_PERIOD_MS) {
    uint8_t newAmp = irAmp;
    if (irValue > (long)IR_TARGET_HIGH && irAmp > IR_AMP_MIN) {
      newAmp = (uint8_t)max((int)IR_AMP_MIN, (int)irAmp - 0x04);
    } else if (irValue < (long)IR_TARGET_LOW && irAmp < IR_AMP_MAX) {
      newAmp = (uint8_t)min((int)IR_AMP_MAX, (int)irAmp + 0x04);
    }
    if (newAmp != irAmp) {
      irAmp = newAmp;
      particleSensor.setPulseAmplitudeIR(irAmp);
    }
    lastAutoIr = nowAuto;
  }
#endif

  // Rate-limited, clean ASCII printing (avoids serial buffer overrun/garbling)
  unsigned long nowMs = millis();
  if (nowMs - lastPrint >= PRINT_PERIOD_MS) {
    Serial.print("IR=");
    Serial.print(irValue);
    Serial.print(", BPM=");
    Serial.print(beatsPerMinute, 1);
    Serial.print(", Avg BPM=");
    Serial.print(beatAvg);
    Serial.print(", IRamp=0x");
    if (irAmp < 0x10) Serial.print('0');
    Serial.print(irAmp, HEX);
    Serial.print(", AC=");
    Serial.print(acAbsLPF, 0);
    if (HR_DEBUG) { Serial.print(", F="); Serial.print(fingerOn ? 1 : 0); }
    if (!fingerOn) Serial.print("  (No finger)");
    Serial.println();
    lastPrint = nowMs;
  }
}