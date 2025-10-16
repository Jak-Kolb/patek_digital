#pragma once

#include <cstddef>
#include <cstdint>

#include "ringbuf/reg_buffer.h"

namespace consolidate {

constexpr size_t kSamplesPerWindow = 60;  // 30 s @ 2 Hz

#pragma pack(push, 1)
struct ConsolidatedRecord {
    uint16_t avg_hr_x10;
    int16_t avg_temp_x100;
    uint16_t step_count;
    uint32_t timestamp_ms;
};
#pragma pack(pop)

static_assert(sizeof(ConsolidatedRecord) == 10, "ConsolidatedRecord must be 10 bytes");

bool consolidate(const reg_buffer::Sample* samples,
                                 size_t sample_count,
                                 ConsolidatedRecord& record_out);

bool consolidate_from_ring(reg_buffer::SampleRingBuffer& ring,
                                                     ConsolidatedRecord& record_out);

}  // namespace consolidate
