#include "consolidate.h"

#include <algorithm>

#include "app_config.h"

namespace consolidate {

void consolidate(const uint8_t* input_buffer, size_t length, int32_t out[4]) {

  if (input_buffer == nullptr || length != kRegisterSize) {
    return;
  }
  
  size_t num = 0;
  for (size_t i = 0; i < 256; i++) { // consolidate 256 byte buffer into 4 int32_t values
    num += input_buffer[i];
    if (i % 64 == 63)
    {
      out[i / 64] = num / 64;
      num = 0;
    }
  }
  Serial.println("Consolidated data:");
  for (size_t i = 0; i < 4; i++) {
    Serial.printf("%d ", out[i]);
  }
  Serial.println();
}

}