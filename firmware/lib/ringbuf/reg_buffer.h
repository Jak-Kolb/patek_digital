#pragma once
#include <cstddef>
#include <cstdint>
#include "../../include/app_config.h"

// Fixed-size (256 B) page ring buffer for raw sensor frames.
// Overwrites oldest on overflow (non-blocking, simplest behavior).

namespace reg_buffer {

// Optional init/clear
void begin();
void clear();

// Producer: push exactly one 256-byte page.
// Returns true always; on overflow it overwrites the oldest page.
bool push_256(const uint8_t* data);

// Consumer: pop one 256-byte page into 'out'. Returns true if a page was popped.
bool pop_256(uint8_t* out);

// Introspection
size_t size();     // number of pages currently stored
size_t capacity(); // max pages (REG_BUFFER_SLOTS)

// Existing helper kept as-is (generates a 256-byte test pattern)
void generate_random_256_bytes(uint8_t* buffer, size_t length);

} // namespace reg_buffer
