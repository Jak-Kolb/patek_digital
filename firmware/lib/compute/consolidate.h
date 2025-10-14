#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Performs a placeholder consolidation of raw register data into a compact payload.
// The current implementation is deterministic but trivial; replace with sensor-aware logic later.

namespace consolidate {
    void consolidate(const uint8_t* input_buffer, size_t length, int32_t out[4]);

}  // namespace consolidate
