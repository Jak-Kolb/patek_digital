#include "consolidate.h"
#include <Arduino.h>
#include <array>
#include <ctime>
#include <algorithm>

namespace consolidate {

namespace {
  constexpr int32_t kStepOnThresholdSq = 600 * 600;   // Horizontal accel spike
  constexpr int32_t kStepOffThresholdSq = 400 * 400;

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
  uint64_t hr_sum = 0;
  int64_t temp_sum = 0;
  uint16_t steps = 0;
  bool step_high = false;

  for (size_t i = 0; i < sample_count; ++i) {
    const auto& s = samples[i];
    hr_sum += s.hr_x10;
    temp_sum += s.temp_x100;

    // Step detection using horizontal acceleration magnitude
    const int32_t horizontal_mag_sq = 
        static_cast<int32_t>(s.ax) * s.ax + static_cast<int32_t>(s.ay) * s.ay;

    if (!step_high && horizontal_mag_sq >= kStepOnThresholdSq) {
      steps++;
      step_high = true;
    } else if (step_high && horizontal_mag_sq <= kStepOffThresholdSq) {
      step_high = false;
    }
  }

  // Calculate averages and build record
  record_out.avg_hr_x10 = static_cast<uint16_t>(
      clamp<uint64_t>(hr_sum / sample_count, 0, UINT16_MAX));
  record_out.avg_temp_x100 = static_cast<int16_t>(
      clamp<int64_t>(temp_sum / sample_count, INT16_MIN, INT16_MAX));
  record_out.step_count = steps;
  record_out.epoch_min = samples[sample_count - 1].epoch_min;

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
