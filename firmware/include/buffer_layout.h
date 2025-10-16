#pragma once
#include <stdint.h>
#include "app_config.h"

// Buffer layout (20 bytes per sample) — matches buffer_layout.txt
// Order and sizes:
// - 2 bytes: heart rate (uint16, BPM * 1)
// - 2 bytes: temperature raw (int16, F * 100)
// - 6 bytes: accel (ax, ay, az) int16 each (mg)
// - 6 bytes: gyro  (gx, gy, gz) int16 each (deci-dps)
// - 4 bytes: timestamp (uint32 ms)

#pragma pack(push, 1)
struct SamplePacked {
  uint16_t hr;         // BPM * 1
  int16_t  temp_raw;   // F * 100 (Fahrenheit)
  int16_t  ax, ay, az; // mg
  int16_t  gx, gy, gz; // deci-dps
  uint32_t ts_ms;      // ms
};
#pragma pack(pop)

static_assert(sizeof(SamplePacked) == 20, "SamplePacked must be 20 bytes");

// Page math for 256B pages: 16B header + 12 * 20B samples = 256B
constexpr size_t kPageHeaderBytes = 16;
constexpr size_t kSampleBytes = sizeof(SamplePacked);
constexpr size_t kSamplesPerPage = (REG_BUFFER_PAGE_BYTES - kPageHeaderBytes) / kSampleBytes;
static_assert((REG_BUFFER_PAGE_BYTES - kPageHeaderBytes) % kSampleBytes == 0, "Page not divisible by sample size");