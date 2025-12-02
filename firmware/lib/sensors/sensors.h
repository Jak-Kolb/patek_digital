#pragma once

#include "ringbuf/reg_buffer.h"

void sensors_setup(reg_buffer::SampleRingBuffer* buffer);
void sensors_loop();
