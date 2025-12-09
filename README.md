# Patek Digital Workspace

Mono-repo containing the ESP32 firmware, iOS companion app, and web portals for the Patek Digital project.

## Repository Layout

- `firmware/` — PlatformIO C++ project targeting ESP32 DevKit boards. Handles sensor data collection, local storage, and BLE communication.
- `apps/iosApp/` — SwiftUI iOS application ("PatekDigitale") for data visualization, device management, and syncing data via Bluetooth.
- `apps/webApp/` — Next.js web application for data dashboarding.
- `apps/test_app/` — Simple Node.js/HTML test utility for BLE/UUID testing.
- `backend/` — (Local only) Python backend for data processing and Supabase integration. Note: This folder is not tracked in git.

## Quick Start

### Firmware (ESP32)

The `firmware/` directory contains scripts to erase flash, build+flash firmware, and open a serial monitor. These scripts wrap PlatformIO commands.

**Requirements:**

- PlatformIO (CLI or VS Code extension)
- USB drivers for your ESP32 board (CP210x or CH340)

**Build & Flash:**
From the repo root:

```bash
cd firmware
# Erase the whole flash (recommended after changing partition layout)
./scripts/erase.sh

# Build and flash the firmware
./scripts/build_flash.sh

# Open serial monitor at 115200
./scripts/monitor.sh
```

### iOS App

Located in `apps/iosApp/PatekDigitale`.

**Usage:**

1. Open `apps/iosApp/PatekDigitale/PatekDigitale.xcodeproj` in Xcode.
2. Ensure you have a valid development signing certificate.
3. Build and run on a physical iOS device (Bluetooth features may not work fully in the simulator).

### Web App (Next.js)

Located in `apps/webApp`.

```bash
cd apps/webApp
npm install
npm run dev
```

### Test App

Located in `apps/test_app`. A simple utility for testing connections.

```bash
cd apps/test_app
npm install
node server.js
```

## System Architecture

The system consists of:

1.  **ESP32 Data Node**: Collects sensor data (AHT20, etc.), buffers it in a ring buffer, and stores it to the filesystem. It advertises via BLE for data syncing.
2.  **iOS App**: Connects to the ESP32 via BLE to sync stored data, view real-time metrics, and manage device settings.
3.  **Supabase**: Used as the cloud database. The iOS app and local backend can push data to Supabase for persistent storage and analysis.

## Development Setup (macOS)

Recommended tools:

- **Homebrew**: `brew install python git`
- **PlatformIO**: `pip install platformio`
- **Node.js**: `brew install node`
- **Xcode**: For iOS development.

## Troubleshooting

- If flashing fails: check your USB cable (use a data cable), switch USB ports, and verify you have the correct serial driver installed.
- If LittleFS won't mount after partition changes: rebuild, erase flash, then flash again.
- If you see a crash with a backtrace in the serial monitor, copy the output and run `./scripts/monitor.sh` with the `esp32_exception_decoder` filter enabled (the scripts already include this filter).

## License

Released under the MIT License. See `LICENSE` for details.
