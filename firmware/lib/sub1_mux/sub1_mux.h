#pragma once
#include <stdint.h>
#include "sensors/sensors.h"   // <-- pulls in Sub1Sample

// Initialize the 256B page and sequence counter
void sub1_mux_begin();

// Add one sample; when the 256B page fills, it flushes to reg_buffer
void sub1_mux_add(const Sub1Sample& s);

// Force-flush current partial page (pads with 0x00); optional
void sub1_mux_flush();
