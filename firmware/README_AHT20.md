# AHT20 Demo Environment

To use the AHT20 demo, add the following to your `platformio.ini` or include this file:

```
[env:aht20_demo]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_speed = 921600
build_src_filter = -<*>, +<src/aht20_demo.cpp>
lib_deps =
  adafruit/Adafruit AHTX0 Library @ ^2.0.0
```

This will build and upload the `aht20_demo.cpp` which prints temperature and humidity to the serial monitor.

**How to verify:**
- Open the serial monitor at 115200 baud.
- You should see output like:
  `Temp: 23.45 C (74.21 F), Humidity: 45.67%`
- If the sensor is not detected, the program will print `AHT20 not found` and halt.
