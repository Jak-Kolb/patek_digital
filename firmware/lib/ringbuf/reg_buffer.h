#pragma once

#include <cstddef>
#include <cstdint>

#include "app_config.h"

// Populate the simulated register buffer with caller-provided data. Passing nullptr
// will synthesize a deterministic demo pattern.
void regbuf_write_mock(const uint8_t* data, size_t length);

// Snapshot the current register contents into the provided output buffer (must be kRegisterSize long).
void regbuf_snapshot(uint8_t* out);
