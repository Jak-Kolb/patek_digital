# ESP32 Data Node Workspace

Mono-repo containing the ESP32 firmware, Fastify backend scaffold, Next.js web portal, and SwiftUI iOS companion for the ESP32 Data Node project.

## Repository Layout

- `firmware/` — PlatformIO Arduino project targeting ESP32 DevKit boards.
- `backend/` — Fastify + TypeScript server ready to ingest payloads and push to Supabase.
- `apps/web/` — Next.js placeholder app for visualizing stored telemetry.
- `apps/ios/` — SwiftUI skeleton meant for future native experiences.
- `.vscode/` — Workspace settings and extension recommendations.

## Quick start — what this repo contains

This repository contains several subprojects related to the ESP32 Data Node prototype. The important directories are listed above. The rest of this README explains how to prepare your macOS development machine for building and flashing the ESP32 firmware and how to run the other services locally.

## Requirements (macOS / zsh)

Minimum tools you should install on your development Mac:

- Homebrew (recommended): package manager used below. Install from https://brew.sh
- Python 3 (system Python is fine but a recent version is preferred)
- Git
- PlatformIO (for building/flashing the ESP32)
- Visual Studio Code (recommended) with the PlatformIO extension (optional but convenient)
- USB serial drivers if your board uses CP210x or CH340 (usually macOS includes CP210x; install drivers for CH340 if needed)

Recommended install commands (macOS / zsh):

```bash
# Install Homebrew (if you don't have it)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install Python and git
brew update
brew install python git

# Install PlatformIO Core (CLI)
python3 -m pip install --upgrade pip
python3 -m pip install platformio

# Optional: Visual Studio Code + PlatformIO extension (install VS Code from https://code.visualstudio.com)
# code --install-extension platformio.platformio-ide
```

Notes:

- PlatformIO installs the toolchain for ESP32 automatically on first build. The CLI install above is sufficient for CI and command-line builds.
- `arduino-cli` is optional; this project uses PlatformIO as its build system.

## Firmware: build / erase / flash / monitor

The `firmware/` directory contains scripts to erase flash, build+flash firmware, and open a serial monitor. These scripts wrap PlatformIO commands and are the easiest way to work with the board.

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

If you prefer PlatformIO commands directly:

```bash
# Build (local environment 'esp32dev')
platformio run -e esp32dev

# Erase flash
platformio run -e esp32dev -t erase

# Upload firmware
platformio run -e esp32dev -t upload

# Monitor serial output
platformio device monitor -e esp32dev --baud 115200
```

Important notes:

- When you change `firmware/partitions_3m_fs.csv` (partition layout), you must rebuild the firmware and then erase the flash before flashing the new build. PlatformIO embeds the partition table at build time; an erase ensures old data/layout is removed.
- If LittleFS fails to mount with errors about `partition "spiffs" could not be found`, ensure every `LittleFS.begin(...)` call in the code uses the explicit partition name (the firmware already does this) and that you rebuilt after updating the CSV.

## Running the backend and web UI

Backend (Fastify + TypeScript):

```bash
cd backend
pnpm install   # or npm install
pnpm dev       # or npm run dev
```

Web UI (Next.js):

```bash
cd web
pnpm install
pnpm dev
```

## Firmware internals and docs

There is a dedicated firmware README describing each firmware source file, functions, and how they fit together. See `firmware/README.md` for detailed developer documentation.

## Troubleshooting

- If flashing fails: check your USB cable (use a data cable), switch USB ports, and verify you have the correct serial driver installed.
- If LittleFS won't mount after partition changes: rebuild, erase flash, then flash again.
- If you see a crash with a backtrace in the serial monitor, copy the output and run `./scripts/monitor.sh` with the `esp32_exception_decoder` filter enabled (the scripts already include this filter).

## License

Released under the MIT License. See `LICENSE` for details.
