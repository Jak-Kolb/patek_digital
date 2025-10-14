# Firmware module documentation

This document explains the firmware layout, what each source file does, and the important functions you will interact with while developing.

Location

- `firmware/` — PlatformIO project using the Arduino framework for ESP32.

Build / Flash / Monitor

- See the repo root `README.md` for quick commands. In short:
  - `./scripts/erase.sh` — erase flash (recommended after partition changes)
  - `./scripts/build_flash.sh` — build and flash
  - `./scripts/monitor.sh` — open serial monitor

Partition layout

- Partition table: `partitions_3m_fs.csv` — contains a 2MB LittleFS partition labeled `littlefs` at offset `0x200000`.

Files and responsibilities

- `src/main.cpp`

  - setup(): initializes Serial, filesystem (`fs_store::begin(true)`), Wi-Fi (`wifi_mgr::begin()`), and other services.
  - loop(): main application loop. Maintains Wi-Fi via `wifi_mgr::tick()` and drives the primary workload. Example data generation and storage calls are present but commented out — use them for testing.

- `lib/compute/consolidate.cpp` / `consolidate.h`

  - Purpose: Reduce a 256-byte register buffer into a compact 4-element `int32_t` payload.
  - API:
    - `void consolidate(const uint8_t* input_buffer, size_t length, int32_t out[4])`
      - `input_buffer` must be 256 bytes; the function divides the buffer into 4 blocks of 64 bytes and computes a per-block average (placeholder logic). Replace with your real consolidation algorithm.

- `lib/ringbuf/reg_buffer.cpp` / `reg_buffer.h`

  - Purpose: Provide a simple demo register buffer generator for development.
  - API:
    - `void generate_random_256_bytes(uint8_t* buffer, size_t length)` — populates `buffer` (must be 256 bytes) with pseudo-random data using `esp_random()`.

- `lib/wifi/wifi_mgr.cpp` / `wifi_mgr.h`

  - Purpose: Manage Wi-Fi station lifecycle: connect, reconnect, and report status.
  - API:
    - `void begin()` — starts Wi-Fi connection using credentials in `wifi_secrets.h` (if present).
    - `bool is_connected()` — returns true if connected.
    - `bool tick()` — light-weight maintenance to be called from `loop()`; attempts reconnection if disconnected.
  - Notes: `wifi_secrets.h` is optional and should define `WIFI_SSID` and `WIFI_PASS`.

- `lib/ble/ble_service.cpp` / `ble_service.h`

  - Purpose: BLE peripheral implementation using NimBLE. Exposes two characteristics: a data characteristic (read/notify) and a control characteristic (write).
  - API:
    - `void begin(ControlCommandCallback callback)` — initialize NimBLE, create service & characteristics, and begin advertising. `callback` is invoked when a control command is written by a client.
    - `void notifyData(const uint8_t* data, size_t length)` — notify connected clients with binary data.
    - `void notifyText(const std::string& message)` — send a UTF-8 text notification.
    - `bool isClientConnected()` — returns true if a central is connected.
    - `void loop()` — call from `loop()` to run trivial housekeeping (LED timing, etc.).
  - Notes: The BLE implementation flashes an onboard LED to indicate activity and handles basic connection/disconnection events.

- `lib/storage/fs_store.cpp` / `fs_store.h`
  - Purpose: Persistent storage for consolidated data using LittleFS in the partition labeled `littlefs`.
  - Key constants:
    - Partition label used for mounting: `"littlefs"`
    - Data file path: `/stored_data.bin`
    - Partition base flash address used in debug prints: `0x200000` (from `partitions_3m_fs.csv`)
  - API:
    - `bool begin(bool formatOnFail)` — mount LittleFS; if `formatOnFail` is true the filesystem will be formatted when mounting fails. The implementation calls `LittleFS.begin(formatOnFail, "/littlefs", 5, "littlefs")` to explicitly mount the correct partition.
    - `size_t size()` — returns the data file size in bytes (0 if missing).
    - `bool append(const int32_t vals[4])` — append a 4 x int32_t consolidated record (16 bytes) to `/stored_data.bin`.
    - `void printData()` — debug-print all stored records. Prints both the file offset (bytes from file start) and the absolute flash address (partition base + offset) for each record.
    - `bool erase()` — removes the data file.
  - Notes: The file format is currently append-only with fixed-size records (16 bytes). If you change to timestamped entries, update `printData()` and size calculations accordingly.

Helpful developer tips

- Always rebuild after changing `partitions_3m_fs.csv`. PlatformIO embeds the partition table at build time.
- After changing the partition table, erase the device flash before uploading a new firmware (`./scripts/erase.sh`), otherwise the on-flash layout may not match the new table.
- Use `fs_store::printData()` to confirm data location and content. The printed absolute flash addresses help when cross-referencing with low-level flash tools.

Where to go next

- Replace the placeholder consolidation logic in `lib/compute/consolidate.cpp` with your real processing.
- Implement robust error handling and wear-level awareness for LittleFS if you're writing frequently.
- Add a binary-to-JSON export route in `backend/src/routes/ingest.ts` to allow easy ingestion of stored records.

License

- MIT (see top-level `LICENSE`)

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
