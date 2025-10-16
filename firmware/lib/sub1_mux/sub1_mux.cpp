#include <Arduino.h>
#include <string.h>
#include <math.h>          // lrintf
#include "sub1_mux.h"
#include "ringbuf/reg_buffer.h"

// ---------- 256B page layout ----------
// We’ll pack N fixed-size mini-frames per page.
// Keep the mini-frame tight & aligned to avoid wasted bytes.
#pragma pack(push, 1)
struct Mini {
  uint32_t ts;        // 4
  uint32_t ppg;       // 4
  int16_t  ax, ay, az;// 6
  int16_t  temp_x100; // 2   (store temp * 100 to keep frame small)
  uint8_t  flags;     // 1   (reserved for DRDY, err bits, etc.)
  uint8_t  _pad;      // 1   (align to 2 bytes)
};                    // = 18 bytes
#pragma pack(pop)

// 256 / 18 = 14 samples (252 bytes) + 4 bytes header = perfect fit.
static constexpr size_t PAGE_BYTES = 256;
static constexpr size_t MINI_BYTES = sizeof(Mini);
static constexpr size_t SLOTS      = 14;

#pragma pack(push, 1)
struct Page {
  // 4B header: magic + slot count
  uint16_t magic;     // 0x5342 ('S''B') — "Sensor Buffer"
  uint8_t  version;   // 0x01
  uint8_t  count;     // number of filled samples (0..14)
  Mini     m[SLOTS];  // 14 * 18 = 252
};                    // 256 bytes total
#pragma pack(pop)

static Page g_page;
static uint32_t g_seq = 0;

// Use the new fixed-size ring-buffer API
static inline bool rb_push_256(const uint8_t* blk) {
  return reg_buffer::push_256(blk);
}

void sub1_mux_begin() {
  memset(&g_page, 0, sizeof(g_page));
  g_page.magic   = 0x4253; // 'B''S' (little-endian OK)
  g_page.version = 0x01;
  g_page.count   = 0;
  g_seq = 0;
}

static void flush_if_full() {
  if (g_page.count >= SLOTS) {
    // Flush the 256B page to the ring buffer
    rb_push_256(reinterpret_cast<const uint8_t*>(&g_page));
    // Reset page
    memset(&g_page, 0, sizeof(g_page));
    g_page.magic   = 0x4253;
    g_page.version = 0x01;
    g_page.count   = 0;
  }
}

void sub1_mux_add(const Sub1Sample& s) {
  Mini* slot = &g_page.m[g_page.count];
  slot->ts       = s.ts_ms;
  slot->ppg      = s.ppg_raw;
  slot->ax       = s.ax;
  slot->ay       = s.ay;
  slot->az       = s.az;
  slot->temp_x100= (int16_t)lrintf(s.temp_c * 100.0f);
  slot->flags    = 0; // reserved (e.g., bit0=data-ready; bit7=overflow)
  slot->_pad     = 0;

  g_page.count++;
  flush_if_full();
}

void sub1_mux_flush() {
  if (g_page.count == 0) return;
  rb_push_256(reinterpret_cast<const uint8_t*>(&g_page));
  // Reset for next page
  memset(&g_page, 0, sizeof(g_page));
  g_page.magic   = 0x4253;
  g_page.version = 0x01;
  g_page.count   = 0;
}
