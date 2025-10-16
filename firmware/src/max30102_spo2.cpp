#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

static constexpr int SDA_PIN = 21;
static constexpr int SCL_PIN = 22;

// Global sample rate used by the algorithm (samples per second)
const int SAMPLE_RATE = 100; // change to 50/200 if you want to experiment

// Toggle plot mode (prints CSV: RED,IR) for visual inspection in Serial Plotter
const bool PLOT_MODE = false;

MAX30105 particleSensor;

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
uint16_t irBuffer[100];   //infrared LED sensor data
uint16_t redBuffer[100];  //red LED sensor data
#else
uint32_t irBuffer[100];   //infrared LED sensor data
uint32_t redBuffer[100];  //red LED sensor data
#endif

int32_t bufferLength = 100;   //data length
int32_t spo2 = 0;             //SPO2 value
int8_t validSPO2 = 0;         //indicator to show if the SPO2 calculation is valid
int32_t heartRate = 0;        //heart rate value
int8_t validHeartRate = 0;    //indicator to show if the heart rate calculation is valid

// smoothing buffer for HR
const int HR_BUF = 7; // slightly larger median window
int hr_buf[HR_BUF];
int hr_idx = 0;
int hr_count = 0;

int median_hr() {
  if (hr_count == 0) return 0;
  int tmp[HR_BUF];
  // copy the last hr_count values from circular buffer in order
  int start = (hr_idx - hr_count + HR_BUF) % HR_BUF;
  for (int i = 0; i < hr_count; ++i) {
    tmp[i] = hr_buf[(start + i) % HR_BUF];
  }
  // simple sort for small array
  for (int i = 0; i < hr_count - 1; ++i) {
    for (int j = i + 1; j < hr_count; ++j) {
      if (tmp[j] < tmp[i]) { int t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
    }
  }
  return tmp[hr_count/2];
}

// Outlier rejection / stability limits
int last_accepted_hr = 0;
const int MAX_ACCEPT_DELTA = 20; // maximum allowed jump (bpm) vs last accepted
const int MIN_ACCEPT_HR = 50;    // raise min plausible HR
const int MAX_ACCEPT_HR = 180;
int outlier_count = 0;

// require two consecutive similar readings before accepting to avoid transients
int candidate_hr = 0;
int accept_streak = 0;

// add globals near top
float ir_ema = 0.0f;
const float EMA_ALPHA = 0.03f; // 0.01..0.1, smaller = slower DC tracking

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("\n[ MAX30102 SpO2 + HR buffered demo ]");

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found. Check wiring/power.");
    while (1) delay(10);
  }

  Serial.println("Attach sensor to finger with rubber band and keep still.");

  byte ledBrightness = 60;  // 0..255 (max 50mA on many breakouts)
  byte sampleAverage = 8;   // 1,2,4,8,16,32 (increase averaging for better SNR)
  byte ledMode = 2;         // 1=Red only, 2=Red+IR
  byte sampleRate = SAMPLE_RATE;    // 50,100,200,400,800...
  int pulseWidth = 411;     // 69,118,215,411
  int adcRange = 16384;     // 2048,4096,8192,16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  // moderate LED values; user can tune later
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeIR(0x45);
}

void loop() {
  // Fill the initial buffers
  for (int i = 0; i < bufferLength; i++) {
    // Wait for new data
    while (particleSensor.available() == false) particleSensor.check();

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();

    // Simple progress print
    if ((i & 0x0F) == 0) Serial.print('.');
  }
  Serial.println();

  // Calculate HR and SpO2 from 100 samples
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer,
    &spo2, &validSPO2, &heartRate, &validHeartRate);

  // Also compute HR using a simple peak-detection on the IR buffer
  // This is an alternative to the maxim algorithm to cross-check results.
  auto compute_peak_hr = [&](uint32_t *buf, int len, int sampleRate)->int {
    // copy into float array and detrend (subtract mean)
    float s[len];
    float sum = 0.0f;
    for (int i = 0; i < len; ++i) { s[i] = (float)buf[i]; sum += s[i]; }
    float mean = sum / len;
    for (int i = 0; i < len; ++i) s[i] -= mean;

  // simple smoothing (5-point moving average) to suppress small sub-peaks
  for (int i = 2; i < len-2; ++i) s[i] = (s[i-2] + s[i-1] + s[i] + s[i+1] + s[i+2]) / 5.0f;

    // find max to set threshold
    float mx = 0.0f;
    for (int i = 0; i < len; ++i) if (s[i] > mx) mx = s[i];
    if (mx <= 0.0f) return 0; // no signal

  float threshold = mx * 0.50f; // peaks above 50% of max to avoid sub-peaks

  // detect peaks with minimum distance (samples)
  int min_dist = max(1, (int)(sampleRate * 0.60f)); // 0.60s minimum between peaks to avoid double-counting
    int peaks[32]; int peakc = 0;
    int last_peak = -min_dist*2;
    for (int i = 1; i < len-1; ++i) {
      if (s[i] > threshold && s[i] > s[i-1] && s[i] >= s[i+1]) {
        if (i - last_peak >= min_dist) {
          if (peakc < 32) peaks[peakc++] = i;
          last_peak = i;
        }
      }
    }
  if (peakc < 4) return 0; // need at least 4 peaks for reliable avg (more robust)

    // compute distances between successive peaks and ignore outliers
    float dists[32]; int dc = 0;
    for (int i = 1; i < peakc; ++i) dists[dc++] = (float)(peaks[i] - peaks[i-1]);
    // compute median distance
    for (int i = 0; i < dc - 1; ++i) for (int j = i+1; j < dc; ++j) if (dists[j] < dists[i]) { float t=dists[i]; dists[i]=dists[j]; dists[j]=t; }
    float med = dists[dc/2];
    float avg_dist = 0.0f; int used = 0;
    for (int i = 0; i < dc; ++i) {
      if (dists[i] > med*0.5f && dists[i] < med*1.5f) { avg_dist += dists[i]; used++; }
    }
    if (used == 0) return 0;
    avg_dist /= used;
    if (avg_dist <= 0.0f) return 0;
    float hr = 60.0f * (float)sampleRate / avg_dist;
    if (hr < 30.0f || hr > 220.0f) return 0;
    return (int)(hr + 0.5f);
  };

  // Autocorrelation-based HR estimator (fallback)
  auto compute_autocorr_hr = [&](uint32_t *buf, int len, int sampleRate)->int {
    // detrend
    float s[len]; float sum=0;
    for (int i=0;i<len;++i) { s[i]=(float)buf[i]; sum+=s[i]; }
    float mean=sum/len; for (int i=0;i<len;++i) s[i]-=mean;
    // autocorr for lags in plausible range (0.35s..1.5s)
    int min_lag = max(2, (int)(sampleRate*0.35f));
    int max_lag = min(len/2, (int)(sampleRate*1.5f));
    float bestR = 0.0f; int bestLag = 0;
    for (int lag=min_lag; lag<=max_lag; ++lag) {
      float r=0.0f;
      for (int i=0;i+lag<len;++i) r += s[i]*s[i+lag];
      if (r > bestR) { bestR = r; bestLag = lag; }
    }
    if (bestLag <= 0) return 0;
    float hr = 60.0f * (float)sampleRate / (float)bestLag;
    if (hr < 30.0f || hr > 220.0f) return 0;
    return (int)(hr+0.5f);
  };

  int hr_peaks = compute_peak_hr(irBuffer, bufferLength, SAMPLE_RATE);

  // smoothing for peak-based HR: small median buffer
  const int HP_BUF = 5;
  static int hp_buf[HP_BUF];
  static int hp_idx = 0;
  static int hp_count = 0;
  if (hr_peaks > 0) {
    hp_buf[hp_idx++] = hr_peaks;
    if (hp_idx >= HP_BUF) hp_idx = 0;
    if (hp_count < HP_BUF) hp_count++;
  }
  // compute median of hp_buf
  int hr_peaks_med = 0;
  if (hp_count > 0) {
    int tmp[HP_BUF];
    int start = (hp_idx - hp_count + HP_BUF) % HP_BUF;
    for (int i = 0; i < hp_count; ++i) tmp[i] = hp_buf[(start + i) % HP_BUF];
    for (int i = 0; i < hp_count - 1; ++i) for (int j = i+1; j < hp_count; ++j) if (tmp[j] < tmp[i]) { int t=tmp[i]; tmp[i]=tmp[j]; tmp[j]=t; }
    hr_peaks_med = tmp[hp_count/2];
  }

  Serial.print("HR_maxim="); Serial.print(heartRate);
  Serial.print(" (valid="); Serial.print(validHeartRate);
  Serial.print("), HR_peaks="); Serial.print(hr_peaks);
  Serial.print(", SpO2="); Serial.print(spo2);
  Serial.print(" (valid="); Serial.print(validSPO2);
  Serial.println(")");

  // Continuous sampling loop: shift and add 25 new samples then recalc
  while (1) {
    // shift last 75 samples to the start
    for (int i = 25; i < 100; i++) {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }

    // take 25 new samples
    for (int i = 75; i < 100; i++) {
      while (particleSensor.available() == false) particleSensor.check();
      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }

    // Calculate
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer,
      &spo2, &validSPO2, &heartRate, &validHeartRate);

    // Print results with smoothing and outlier rejection
    // Acceptance: require valid flag and within absolute range
    bool accepted = false;
    if (validHeartRate == 1 && heartRate >= MIN_ACCEPT_HR && heartRate <= MAX_ACCEPT_HR) {
      if (last_accepted_hr == 0 || abs(heartRate - last_accepted_hr) <= MAX_ACCEPT_DELTA) {
        // Candidate: either close to last accepted or first measurement
        if (candidate_hr == 0 || abs(candidate_hr - heartRate) <= 4) {
          // consecutive similar readings -> increase streak
          candidate_hr = heartRate;
          accept_streak++;
        } else {
          // new candidate, reset streak
          candidate_hr = heartRate;
          accept_streak = 1;
        }

        if (accept_streak >= 2) {
          // accept
          hr_buf[hr_idx++] = candidate_hr;
          if (hr_idx >= HR_BUF) hr_idx = 0;
          if (hr_count < HR_BUF) hr_count++;
          last_accepted_hr = candidate_hr;
          accepted = true;
          outlier_count = 0;
          accept_streak = 0; // reset streak after acceptance
          candidate_hr = 0;
        }
      } else {
        // jump too large: treat as outlier
        outlier_count++;
        candidate_hr = heartRate;
        accept_streak = 1;
      }
    } else {
      outlier_count++;
    }

    int hr_smoothed = median_hr();

    // Decide primary HR: prefer peak-based median if available and plausible
    int primary_hr = 0;
    if (hr_peaks_med >= 40 && hr_peaks_med <= 180) primary_hr = hr_peaks_med;
    else if (hr_smoothed >= 40 && hr_smoothed <= 180) primary_hr = hr_smoothed;
    else primary_hr = 0; // unknown

    // Optionally print raw CSV for plot mode
    if (PLOT_MODE) {
      // print last 100 samples as CSV red,ir (one line per sample)
      for (int i = 0; i < bufferLength; ++i) {
        Serial.print(redBuffer[i]); Serial.print(','); Serial.println(irBuffer[i]);
      }
      Serial.println("---");
    }

    Serial.print("HR="); Serial.print(primary_hr);
    Serial.print(" (peaks="); Serial.print(hr_peaks_med);
    Serial.print(", maxim="); Serial.print(heartRate);
    Serial.print(" valid="); Serial.print(validHeartRate);
    Serial.print(accepted ? ", ACCEPTED" : ", REJECTED");
    Serial.print("), SpO2="); Serial.print(spo2);
    Serial.print(" (valid="); Serial.print(validSPO2);
    Serial.println(")");

    delay(1000); // show results once per second
  }
}
