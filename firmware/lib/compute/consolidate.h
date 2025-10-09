#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Performs a placeholder consolidation of raw register data into a compact payload.
// The current implementation is deterministic but trivial; replace with sensor-aware logic later.
void consolidate(const uint8_t* input, size_t length, std::vector<uint8_t>& output);
