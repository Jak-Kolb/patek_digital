// #include "reg_buffer.h"

#include <Arduino.h>
#include <array>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include "app_config.h"

namespace reg_buffer {

void generate_random_256_bytes(uint8_t* buffer, size_t length) {

  if (buffer == nullptr || length != 256) {
    Serial.println("Invalid buffer for random data generation.");
    return;
  }

  for (size_t i = 0; i < 256; i += 4) {
    uint32_t val = esp_random();
    std::memcpy(&buffer[i], &val, sizeof(val));
  }
}


}  // namespace reg_buffer