#pragma once

#include <array>
#include <array>
#include <cstddef>
#include <cstdint>

namespace reg_buffer {

// Sensor sample captured from the acquisition pipeline.
#pragma pack(push, 1)

// Portable half-precision wrapper (stores IEEE 754 binary16 in 16 bits).
// Provides implicit conversion to/from float for ease of use.
struct float16 {
    uint16_t bits{0};
    float16() = default;
    explicit float16(float f) : bits(float_to_half(f)) {}
    operator float() const { return half_to_float(bits); }
    // Static conversion helpers
    static uint16_t float_to_half(float f) {
        union { float f; uint32_t u; } v{f};
        uint32_t sign = (v.u >> 31) & 0x1;
        int32_t exp = (int32_t)((v.u >> 23) & 0xFF) - 127; // unbiased exponent
        uint32_t mant = v.u & 0x7FFFFF;
        uint16_t h;
        if (exp > 15) { // overflow -> inf
            h = (uint16_t)((sign << 15) | (0x1F << 10));
        } else if (exp > -15) { // normal range
            int32_t he = exp + 15;
            uint32_t hm = mant >> 13; // truncate mantissa
            // round: add LSB guard
            uint32_t roundBit = (mant >> 12) & 1;
            hm += roundBit; // simplistic rounding
            if (hm & 0x0400) { // mantissa overflow
                hm = 0; ++he; if (he >= 31) { he = 31; }
            }
            h = (uint16_t)((sign << 15) | ((he & 0x1F) << 10) | (hm & 0x3FF));
        } else { // subnormal or zero
            if (exp < -24) {
                h = (uint16_t)(sign << 15); // underflow to zero
            } else {
                // produce subnormal
                int shift = (-exp - 15);
                uint32_t sub = (0x800000 | mant) >> (shift + 13);
                h = (uint16_t)((sign << 15) | sub);
            }
        }
        return h;
    }
    static float half_to_float(uint16_t h) {
        uint32_t sign = (h >> 15) & 0x1;
        uint32_t exp = (h >> 10) & 0x1F;
        uint32_t mant = h & 0x3FF;
        uint32_t u;
        if (exp == 0) {
            if (mant == 0) {
                u = sign << 31; // zero
            } else { // subnormal -> normal float
                // Normalize mantissa
                exp = 1;
                while ((mant & 0x400) == 0) { mant <<= 1; --exp; }
                mant &= 0x3FF;
                uint32_t fexp = (exp - 15 + 127);
                u = (sign << 31) | (fexp << 23) | (mant << 13);
            }
        } else if (exp == 0x1F) { // inf/NaN
            u = (sign << 31) | (0xFF << 23) | (mant << 13);
        } else {
            uint32_t fexp = (exp - 15 + 127);
            u = (sign << 31) | (fexp << 23) | (mant << 13);
        }
        union { uint32_t u; float f; } v{u};
        return v.f;
    }
};

struct Sample {
    float16 ax;       // accel X (g or raw units)
    float16 ay;       // accel Y
    float16 az;       // accel Z
    float16 gx;       // gyro X (dps or raw)
    float16 gy;       // gyro Y
    float16 gz;       // gyro Z
    float16 hr_bpm;   // heart rate BPM
    float16 temp_c;   // body temperature Celsius
    float epoch_min;  // minutes since boot (float32)
};
#pragma pack(pop)

static_assert(sizeof(Sample) == 20, "Sample must remain 20 bytes (8*half + float32)");

// Fixed-size circular buffer tailored for 64 sensor samples.
class SampleRingBuffer {
 public:
    static constexpr size_t kCapacity = 256;

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