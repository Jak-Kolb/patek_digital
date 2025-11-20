#include "reg_buffer.h"

namespace reg_buffer {

SampleRingBuffer::SampleRingBuffer() = default;

bool SampleRingBuffer::push(const Sample& sample) {
  if (full()) {
    return false;
  }
  buffer_[tail_] = sample;
  tail_ = (tail_ + 1) % kCapacity;
  ++count_;
  return true;
}

bool SampleRingBuffer::pop(Sample& sample_out) {
  if (empty()) {
    return false;
  }
  sample_out = buffer_[head_];
  head_ = (head_ + 1) % kCapacity;
  --count_;
  return true;
}

bool SampleRingBuffer::peek(size_t index, Sample& sample_out) const {
  if (index >= count_) {
    return false;
  }
  size_t pos = (head_ + index) % kCapacity;
  sample_out = buffer_[pos];
  return true;
}

void SampleRingBuffer::clear() {
  head_ = 0;
  tail_ = 0;
  count_ = 0;
}

}  // namespace reg_buffer