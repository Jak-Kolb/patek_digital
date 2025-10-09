#include "reg_buffer.h"

#include <Arduino.h>
#include <array>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

namespace {
std::array<uint8_t, kRegisterSize> gRegister{};
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t gPatternCounter = 0;

void synthesize_pattern() {
  gPatternCounter++;
  for (size_t i = 0; i < gRegister.size(); ++i) {
    gRegister[i] = static_cast<uint8_t>((gPatternCounter + i) & 0xFF);
  }
}

}  // namespace

void regbuf_write_mock(const uint8_t* data, size_t length) {
  portENTER_CRITICAL(&gMux);
  if (data != nullptr && length > 0) {
    const size_t copy_len = length > gRegister.size() ? gRegister.size() : length;
    std::memcpy(gRegister.data(), data, copy_len);
    if (copy_len < gRegister.size()) {
      std::memset(gRegister.data() + copy_len, 0, gRegister.size() - copy_len);
    }
  } else {
    synthesize_pattern();
  }
  portEXIT_CRITICAL(&gMux);
}

void regbuf_snapshot(uint8_t* out) {
  if (out == nullptr) {
    return;
  }
  portENTER_CRITICAL(&gMux);
  std::memcpy(out, gRegister.data(), gRegister.size());
  portEXIT_CRITICAL(&gMux);
}
