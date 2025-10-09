#include "consolidate.h"

#include <algorithm>

#include "app_config.h"

void consolidate(const uint8_t* input, size_t length, std::vector<uint8_t>& output) {
  output.clear();
  if (input == nullptr || length == 0) {
    return;
  }

  const size_t copy_len = std::min<size_t>(length, 32);
  output.insert(output.end(), input, input + copy_len);

  uint8_t checksum = 0;
  for (size_t i = 0; i < copy_len; ++i) {
    checksum ^= input[i];
  }
  output.push_back(checksum);

  // TODO: Replace the naive copy/XOR checksum with domain-specific encoding (e.g. delta compression).
  // TODO: Include device metadata (timestamp, sensor flags, error codes) once available.
}
