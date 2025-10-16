#include "consolidate.h"

#include <Arduino.h>
#include <array>
#include <limits>

namespace consolidate {

constexpr int32_t kStepOnThresholdSq = 600 * 600;   // horizontal accel spike
constexpr int32_t kStepOffThresholdSq = 400 * 400;

uint16_t clamp_u16(uint32_t value) {
  return value > static_cast<uint32_t>(std::numeric_limits<uint16_t>::max())
             ? std::numeric_limits<uint16_t>::max()
             : static_cast<uint16_t>(value);
}

int16_t clamp_i16(int32_t value) {
  if (value > static_cast<int32_t>(std::numeric_limits<int16_t>::max())) {
    return std::numeric_limits<int16_t>::max();
  }
  if (value < static_cast<int32_t>(std::numeric_limits<int16_t>::min())) {
    return std::numeric_limits<int16_t>::min();
  }
  return static_cast<int16_t>(value);
}


bool consolidate(const reg_buffer::Sample* samples,
                 size_t sample_count,
                 ConsolidatedRecord& record_out) {
  if (samples == nullptr || sample_count == 0) {
    return false;
  }

  uint64_t hr_sum = 0;
  int64_t temp_sum = 0;
  uint16_t steps = 0;
  bool step_high = false;

  for (size_t i = 0; i < sample_count; ++i) {
    const auto& s = samples[i];
    hr_sum += s.hr_x10;
    temp_sum += s.temp_x100;

    const int32_t ax = static_cast<int32_t>(s.ax);
    const int32_t ay = static_cast<int32_t>(s.ay);
    const int32_t horizontal_mag_sq = ax * ax + ay * ay;

    if (!step_high && horizontal_mag_sq >= kStepOnThresholdSq) {
      ++steps;
      step_high = true;
    } else if (step_high && horizontal_mag_sq <= kStepOffThresholdSq) {
      step_high = false;
    }
  }

  const uint32_t avg_hr = static_cast<uint32_t>(hr_sum / sample_count);
  const int32_t avg_temp = static_cast<int32_t>(temp_sum / static_cast<int64_t>(sample_count));

  record_out.avg_hr_x10 = clamp_u16(avg_hr);
  record_out.avg_temp_x100 = clamp_i16(avg_temp);
  record_out.step_count = steps;
  record_out.timestamp_ms = samples[sample_count - 1].ts_ms;

  Serial.printf("Consolidated window: HR=%.1f bpm, Temp=%.2f C, Steps=%u, ts=%lu\n",
                record_out.avg_hr_x10 / 10.0f,
                record_out.avg_temp_x100 / 100.0f,
                record_out.step_count,
                static_cast<unsigned long>(record_out.timestamp_ms));

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
