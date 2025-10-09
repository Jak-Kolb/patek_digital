#include <Arduino.h>
#include <unity.h>

#include <vector>

#include "app_config.h"
#include "compute/consolidate.h"

void setUp() {}
void tearDown() {}

void test_consolidate_checksum() {
  uint8_t input[kRegisterSize];
  for (size_t i = 0; i < kRegisterSize; ++i) {
    input[i] = static_cast<uint8_t>(i & 0xFF);
  }

  std::vector<uint8_t> out;
  consolidate(input, kRegisterSize, out);

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(33, out.size(), "Expect 32 bytes + checksum");
  for (size_t i = 0; i < 32; ++i) {
    TEST_ASSERT_EQUAL_UINT8(input[i], out[i]);
  }
  uint8_t expected_checksum = 0;
  for (size_t i = 0; i < 32; ++i) {
    expected_checksum ^= input[i];
  }
  TEST_ASSERT_EQUAL_UINT8(expected_checksum, out.back());
}

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_consolidate_checksum);
  UNITY_END();
}

void loop() {}
