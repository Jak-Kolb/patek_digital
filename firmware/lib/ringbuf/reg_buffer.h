#pragma once

#include <cstddef>
#include <cstdint>

#include "app_config.h"

// Populate the simulated register buffer with caller-provided data. Passing nullptr
// will synthesize a deterministic demo pattern.

namespace reg_buffer {
    void generate_random_256_bytes(uint8_t* buffer, size_t length);

}  // namespace reg_buffer
