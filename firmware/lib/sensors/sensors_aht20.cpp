#include <Adafruit_AHTX0.h>
#include <Wire.h>
#include "app_config.h"
#include "sensors.h"

static Adafruit_AHTX0 aht;

static bool aht20_init() {
    Wire.begin(21, 22);
    return aht.begin();
}

static bool aht20_read(float &tc, float &hum) {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    if (isnan(temp.temperature) || isnan(humidity.relative_humidity)) return false;
    tc = temp.temperature;
    hum = humidity.relative_humidity;
    return true;
}

void sensors_init() {
    max30102_init();
    aht20_init();
    bmi270_init();
}

bool read_ppg(uint32_t &ppg) { return max30102_read(ppg); }
bool read_temp(float &tc) { float h; return aht20_read(tc, h); }
bool read_humidity(float &hum) { float t; return aht20_read(t, hum); }
bool read_accel(int16_t &ax, int16_t &ay, int16_t &az) { return bmi270_read_accel(ax, ay, az); }

bool poll_all(Sub1Sample &out) {
    bool ok = false;
    out.ts_ms = millis();

    uint32_t ppg;
    if (read_ppg(ppg)) { out.ppg_raw = ppg; ok = true; }
    float tc, hum;
    if (aht20_read(tc, hum)) { out.temp_c = tc; out.humidity = hum; ok = true; }
    int16_t ax, ay, az;
    if (read_accel(ax, ay, az)) { out.ax = ax; out.ay = ay; out.az = az; ok = true; }
    return ok;
}
