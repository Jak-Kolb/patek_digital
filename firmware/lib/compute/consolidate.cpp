#include "consolidate.h"
#include <Arduino.h>
#include <array>
#include <ctime>
#include <algorithm>
#include <cmath>
#include <vector>

namespace consolidate {

namespace {
  template<typename T>
  T clamp(T value, T min_val, T max_val) {
    return std::max(min_val, std::min(value, max_val));
  }
}

bool consolidate(const reg_buffer::Sample* samples,
                 size_t sample_count,
                 ConsolidatedRecord& record_out) {
  if (!samples || sample_count == 0) {
    return false;
  }

  // Accumulate sensor data
  double hr_sum = 0;
  double temp_sum = 0;
  uint16_t steps = 0;

  // Pass 1: Calculate average magnitude to determine gravity baseline
  double mag_sum = 0;
  // Use a small local buffer for magnitudes to avoid re-calculating sqrt
  // Assuming sample_count is small (e.g. 25)
  std::vector<float> mags(sample_count); 
  
  for(size_t i=0; i<sample_count; ++i) {
      const auto& s = samples[i];
      float ax = (float)s.ax;
      float ay = (float)s.ay;
      float az = (float)s.az;
      float m = std::sqrt(ax*ax + ay*ay + az*az);
      mags[i] = m;
      mag_sum += m;

      hr_sum += (float)s.hr_bpm;
      temp_sum += (float)s.temp_c;
  }
  
  float avg_mag = mag_sum / sample_count;
  
  // Pass 2: Detect steps
  // Threshold: 15% above baseline
  float threshold = avg_mag * 0.15f; 
  
  // Clamp min threshold to avoid noise triggering when still
  // If units are g (avg ~ 1.0), min threshold 0.05g
  // If units are raw (avg ~ 16384), min threshold 800
  if (avg_mag < 100.0f) {
      if (threshold < 0.05f) threshold = 0.05f;
  } else {
      if (threshold < 800.0f) threshold = 800.0f;
  }

  bool in_step = false;
  for(size_t i=0; i<sample_count; ++i) {
      if (!in_step && mags[i] > avg_mag + threshold) {
          steps++;
          in_step = true;
      } else if (in_step && mags[i] < avg_mag) { // Reset when crossing mean
          in_step = false;
      }
  }

  // Calculate averages and build record
  double avg_hr = hr_sum / sample_count;
  double avg_temp = temp_sum / sample_count;

  record_out.avg_hr_x10 = static_cast<uint16_t>(
      clamp<double>(avg_hr * 10.0, 0, 65535.0));
  record_out.avg_temp_x100 = static_cast<int16_t>(
      clamp<double>(avg_temp * 100.0, -32768.0, 32767.0));
  record_out.step_count = steps;
  record_out.epoch_min = static_cast<uint32_t>(samples[sample_count - 1].epoch_min);

  // Debug output with timestamp if available
  char time_buf[24] = "";
  const time_t epoch_sec = static_cast<time_t>(record_out.epoch_min) * 60;
  if (epoch_sec > 0) {
    struct tm* tm_ptr = gmtime(&epoch_sec);
    if (tm_ptr) {
      strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%MZ", tm_ptr);
    }
  }

  Serial.printf("Consolidated: HR=%.1f bpm, Temp=%.2f°C, Steps=%u%s%s\n",
                record_out.avg_hr_x10 / 10.0f,
                record_out.avg_temp_x100 / 100.0f,
                record_out.step_count,
                time_buf[0] ? ", ts=" : ", epoch_min=",
                time_buf[0] ? time_buf : String(record_out.epoch_min).c_str());

  return true;
}

bool consolidate_from_ring(reg_buffer::SampleRingBuffer& ring,
                           ConsolidatedRecord& record_out) {
  if (ring.size() < kSamplesPerWindow) {
    return false;
  }

  std::array<reg_buffer::Sample, kSamplesPerWindow> window{};
  for (size_t i = 0; i < kSamplesPerWindow; ++i) {
    if (!ring.pop(window[i])) {
      return false;
    }
  }

  return consolidate(window.data(), window.size(), record_out);
}

}  // namespace consolidate
