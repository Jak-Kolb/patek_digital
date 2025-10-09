# Firmware: ESP32 Data Node

This PlatformIO project targets the ESP32 DevKit using the Arduino framework. It advertises a BLE service for data retrieval, persists consolidated sensor snapshots to LittleFS, and scaffolds Wi-Fi connectivity for future cloud sync work.

## Features
- NimBLE-based GATT service exposing control and notify characteristics (`LIST`, `SEND`, `ERASE`).
- 256-byte register abstraction with a deterministic mock data pattern.
- Stub consolidation pipeline producing a checksum-bearing payload suitable for unit testing.
- LittleFS helpers for mount/format, append, streaming reads, and erase operations.
- Optional Wi-Fi connection using secrets stored in `secrets/wifi_secrets.h`.
- Convenience scripts for flash erasure, build/upload, and serial monitoring.
- Arduino CLI configuration for contributors preferring Arduino tooling.

## Getting Started
```bash
# Install PlatformIO Core if needed
pip install platformio

# Install ESP32 support for arduino-cli (optional but recommended)
cd firmware
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

Copy Wi-Fi secrets if you plan to enable Wi-Fi:
```bash
cd firmware
cp secrets/wifi_secrets.example.h secrets/wifi_secrets.h
```
Then edit the new file and flip `ENABLE_WIFI` to `1` in `include/app_config.h` when you are ready.

## Build & Flash
```bash
cd firmware
./scripts/erase.sh              # optional but useful when switching sketches
./scripts/build_flash.sh        # compile + upload
./scripts/monitor.sh            # open serial monitor at 115200 baud
```

### BLE Testing
1. Power up the board; it advertises as **ESP32-DataNode**.
2. From iOS/macOS (e.g., LightBlue, nRF Connect, or `bluetoothd` tools), connect and discover the service `12345678-1234-5678-1234-56789abc0000`.
3. Subscribe to characteristic `...1001` (notify) and write the commands below to characteristic `...1002`:
   - `LIST` – returns the byte length of `/consolidated.dat`.
   - `SEND` – streams the stored file in MTU-sized chunks.
   - `ERASE` – clears the file and confirms via notify.

### LittleFS Notes
- Data lives in `/consolidated.dat`; use `SEND` to inspect it without removing the filesystem.
- The filesystem auto-formats on first boot if mounting fails.
- Extend `lib/storage/fs_store.cpp` for rotation or metadata once requirements are known.

## Troubleshooting
- **Upload fails**: Ensure the board is in bootloader mode (hold `BOOT`, tap `EN`). Consider lowering `upload_speed` in `platformio.ini` for long cables.
- **BLE invisible**: Power cycle the ESP32 or clear the bonding list on the central device.
- **Wi-Fi disabled**: Confirm `ENABLE_WIFI` in `app_config.h` and provide `secrets/wifi_secrets.h`.

## Testing
```bash
cd firmware
pio test -e esp32dev
```

Unity executes on-device to validate the consolidation stub.
