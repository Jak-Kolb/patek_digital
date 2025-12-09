#pragma once

#include <cstddef>
#include <cstdint>

#include "ringbuf/reg_buffer.h"

namespace consolidate {

constexpr size_t kSamplesPerWindow = 125;  // 2.5 s @ 25 Hz

#pragma pack(push, 1)
struct ConsolidatedRecord {
    uint16_t avg_hr_x10;
    int16_t avg_temp_x100;
    uint16_t step_count;
    uint32_t timestamp;
};
#pragma pack(pop)

static_assert(sizeof(ConsolidatedRecord) == 10, "ConsolidatedRecord must be 10 bytes");

class IntervalAccumulator {
public:
    void reset();
    bool add(const ConsolidatedRecord& input, ConsolidatedRecord& output);

private:
    uint32_t sum_hr_x10 = 0;
    int32_t sum_temp_x100 = 0;
    uint32_t sum_steps = 0;
    int count = 0;
    
    // 15 seconds / 2.5 seconds per record = 6 records
    static constexpr int kRecordsPerInterval = 6;
};

bool consolidate(const reg_buffer::Sample* samples,
                                 size_t sample_count,
                                 ConsolidatedRecord& record_out);

bool consolidate_from_ring(reg_buffer::SampleRingBuffer& ring,
                                                     ConsolidatedRecord& record_out);

}  // namespace consolidate
