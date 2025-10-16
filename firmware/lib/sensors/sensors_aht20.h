#pragma once
#include <stdint.h>

struct Sub1Sample {
    uint32_t ts_ms;
    uint32_t ppg_raw; // 18-bit from MAX30102 (stored in 32-bit)
    float    temp_c;
    float    humidity;
    int16_t  ax, ay, az;
};

void sensors_init();

bool read_ppg(uint32_t &ppg);
bool read_temp(float &tc);
bool read_humidity(float &hum);
bool read_accel(int16_t &ax, int16_t &ay, int16_t &az);

bool poll_all(Sub1Sample &out);
