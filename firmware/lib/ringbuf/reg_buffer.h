#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace reg_buffer {

// Sensor sample captured from the acquisition pipeline.
#pragma pack(push, 1)
struct Sample {
    int16_t ax;
    int16_t ay;
    int16_t az;
    int16_t gx;
    int16_t gy;
    int16_t gz;
    uint16_t hr_x10;
    int16_t temp_x100;
    uint32_t ts_ms;
};
#pragma pack(pop)

static_assert(sizeof(Sample) == 20, "Sample must remain 20 bytes");

// Fixed-size circular buffer tailored for 64 sensor samples.
class SampleRingBuffer {
 public:
    static constexpr size_t kCapacity = 64;

    SampleRingBuffer();

    bool push(const Sample& sample);        // returns false if buffer is full
    bool pop(Sample& sample_out);           // returns false if buffer is empty
    bool peek(size_t index, Sample& sample_out) const;  // index relative to oldest
    size_t size() const { return count_; }
    bool empty() const { return count_ == 0; }
    bool full() const { return count_ == kCapacity; }
    void clear();

 private:
    std::array<Sample, kCapacity> buffer_{};
    size_t head_ = 0;  // points to oldest element
    size_t tail_ = 0;  // points to next insertion slot
    size_t count_ = 0;
};

}  // namespace reg_buffer
