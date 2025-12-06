#include "consolidate.h"
#include <Arduino.h>
#include <cmath>
#include <algorithm>
#include <array>

namespace consolidate {

namespace {
    // --- WRIST TUNING ---
    constexpr size_t kMaxBufferSize = 256; 

    // FILTERING: How much do we trust the new value vs the old average?
    // 0.1 = Very smooth (removes jitter, good for walking).
    // 0.5 = Very reactive (good for running).
    // 0.15 is a sweet spot for wrist walking.
    constexpr float kFilterAlpha = 0.11f; 

    // DEBOUNCE: Wrist steps are rarely faster than 3 per second (333ms).
    // At 25Hz, 333ms = ~8 samples.
    // We set this slightly lower to catch fast walking.
    constexpr int kMinSamplesBetweenSteps = 6; 

    // THRESHOLD: The minimum rise above average to count as a peak.
    // Wrist signals are weaker than hip signals.
    // 0.03G is very sensitive, necessary if holding a coffee cup.
    constexpr float kMinPeakHeight = 0.03f; 

    struct StepContext {
        uint32_t samples_since_step = 1000;
        float running_avg = 1.0f; // Smoothed magnitude memory
        bool valid_walking = false;
        uint8_t streak = 0;
    };

    static StepContext ctx; 

    template<typename T>
    T clamp(T value, T min_val, T max_val) {
        return std::max(min_val, std::min(value, max_val));
    }
}

bool consolidate(const reg_buffer::Sample* samples,
                 size_t sample_count,
                 ConsolidatedRecord& record_out) {
    
    if (!samples || sample_count == 0) return false;
    if (sample_count > kMaxBufferSize) sample_count = kMaxBufferSize;

    // Detect if using Raw Data (e.g. 16384) or Gs (e.g. 1.0)
    float scale_factor = 1.0f;
    if (std::abs(samples[0].ax) > 500.0f) scale_factor = 2000.0f; 

    double hr_sum = 0;
    double temp_sum = 0;
    
    // We need 3 samples to detect a peak (Previous, Current, Next)
    // We process from index 1 to count-1
    float smooth_mags[kMaxBufferSize];

    // --- PASS 1: Calculate Magnitude & Apply Low-Pass Filter ---
    // This removes the "jitter" of the wrist watch.
    float current_avg = ctx.running_avg;
    
    for (size_t i = 0; i < sample_count; ++i) {
        const auto& s = samples[i];
        
        // 1. Raw Magnitude
        float m = std::sqrt(s.ax*s.ax + s.ay*s.ay + s.az*s.az);

        // 2. Low Pass Filter (The "Smoothing" Step)
        // New = (Old * 0.85) + (Raw * 0.15)
        current_avg = (current_avg * (1.0f - kFilterAlpha)) + (m * kFilterAlpha);
        smooth_mags[i] = current_avg;

        hr_sum += s.hr_bpm;
        temp_sum += s.temp_c;
    }
    // Save the filter state for next time
    ctx.running_avg = current_avg;

    // Calculate a local baseline for THIS window to find relative peaks
    double window_sum = 0;
    for(size_t i=0; i<sample_count; ++i) window_sum += smooth_mags[i];
    float window_baseline = window_sum / sample_count;


    // --- PASS 2: Peak Detection ---
    uint16_t window_steps = 0;
    float peak_threshold = window_baseline + (kMinPeakHeight * scale_factor);

    // We loop from 1 to count-1 because we look at neighbors [i-1] and [i+1]
    for (size_t i = 1; i < sample_count - 1; ++i) {
        ctx.samples_since_step++;

        float prev = smooth_mags[i-1];
        float curr = smooth_mags[i];
        float next = smooth_mags[i+1];

        // Is this a Peak? (Higher than neighbors)
        if (curr > prev && curr > next) {
            
            // Is it a "Real" Peak? (High enough above baseline)
            if (curr > peak_threshold) {
                
                // Debounce (Time Check)
                if (ctx.samples_since_step > kMinSamplesBetweenSteps) {
                    
                    // VALID STEP
                    ctx.samples_since_step = 0;
                    ctx.streak++;

                    // Streak Logic (Lowered to 3 for Wrist)
                    if (ctx.valid_walking) {
                        window_steps++;
                    } else if (ctx.streak >= 3) {
                        ctx.valid_walking = true;
                        window_steps += 3; // Backfill
                    }
                }
            }
        }
    }

    // Timeout logic: If no steps in this whole window, reset streak
    if (window_steps == 0 && ctx.samples_since_step > 50) { // ~2 seconds silence
         ctx.streak = 0;
         ctx.valid_walking = false;
    }

    // --- OUTPUT ---
    record_out.avg_hr_x10 = static_cast<uint16_t>(clamp<double>(hr_sum/sample_count * 10.0, 0, 65535));
    record_out.avg_temp_x100 = static_cast<int16_t>(clamp<double>(temp_sum/sample_count * 100.0, -32768, 32767));
    record_out.step_count = window_steps;
    record_out.timestamp = samples[sample_count - 1].timestamp;

    Serial.printf("[WRIST] Steps:+%u | Streak:%u | Base:%.2f\n", 
                  window_steps, ctx.streak, window_baseline);

    return true;
}

bool consolidate_from_ring(reg_buffer::SampleRingBuffer& ring,
                           ConsolidatedRecord& record_out) {
    if (ring.size() < kSamplesPerWindow) return false;
    static std::array<reg_buffer::Sample, kSamplesPerWindow> window{};
    for (size_t i = 0; i < kSamplesPerWindow; ++i) ring.pop(window[i]);
    return consolidate(window.data(), window.size(), record_out);
}

} // namespace