#include "reg_buffer.h"

#include <Arduino.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

// ===== Internal ring storage =====
static uint8_t g_ring[REG_BUFFER_SLOTS][REG_BUFFER_PAGE_BYTES];
static volatile size_t g_head = 0;   // next write index
static volatile size_t g_tail = 0;   // next read index
static volatile size_t g_count = 0;  // number of stored pages

// Lightweight critical section (ESP32 Arduino)
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

namespace reg_buffer {

void begin() {
  taskENTER_CRITICAL(&g_mux);
  g_head = g_tail = g_count = 0;
  taskEXIT_CRITICAL(&g_mux);
}

void clear() {
  begin();
}

static inline void copy256(uint8_t* dst, const uint8_t* src) {
  // 256 is small; memcpy is fine
  std::memcpy(dst, src, REG_BUFFER_PAGE_BYTES);
}

bool push_256(const uint8_t* data) {
  if (!data) return false;

  taskENTER_CRITICAL(&g_mux);

  // Write into current head slot
  copy256(g_ring[g_head], data);

  // Advance head
  g_head = (g_head + 1) % REG_BUFFER_SLOTS;

  if (g_count < REG_BUFFER_SLOTS) {
    g_count++;
  } else {
    // Overwrite policy: move tail forward when full
    g_tail = (g_tail + 1) % REG_BUFFER_SLOTS;
  }

  taskEXIT_CRITICAL(&g_mux);
  return true;
}

bool pop_256(uint8_t* out) {
  if (!out) return false;

  taskENTER_CRITICAL(&g_mux);
  if (g_count == 0) {
    taskEXIT_CRITICAL(&g_mux);
    return false;
  }

  // Read from tail
  copy256(out, g_ring[g_tail]);
  g_tail = (g_tail + 1) % REG_BUFFER_SLOTS;
  g_count--;

  taskEXIT_CRITICAL(&g_mux);
  return true;
}

size_t size() {
  taskENTER_CRITICAL(&g_mux);
  size_t v = g_count;
  taskEXIT_CRITICAL(&g_mux);
  return v;
}

size_t capacity() {
  return REG_BUFFER_SLOTS;
}

// ======= Existing helper preserved =======
void generate_random_256_bytes(uint8_t* buffer, size_t length) {
  if (buffer == nullptr || length != REG_BUFFER_PAGE_BYTES) {
    Serial.println("Invalid buffer for random data generation.");
    return;
  }
  for (size_t i = 0; i < REG_BUFFER_PAGE_BYTES; i += 4) {
    uint32_t val = esp_random();
    std::memcpy(&buffer[i], &val, sizeof(val));
  }
}

} // namespace reg_buffer
