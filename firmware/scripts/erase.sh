#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if command -v pio >/dev/null 2>&1; then
  echo "[erase] Using PlatformIO target"
  if pio run -t erase; then
    exit 0
  fi
  echo "[erase] PlatformIO erase failed; trying esptool.py"
fi

if ! command -v esptool.py >/dev/null 2>&1; then
  echo "esptool.py not found. Install with 'pip install esptool'" >&2
  exit 1
fi

PORT="${1:-}"
if [[ -z "$PORT" ]]; then
  PORT=$(ls /dev/tty.usbserial* /dev/tty.usbmodem* 2>/dev/null | head -n1 || true)
fi
if [[ -z "$PORT" ]]; then
  echo "Unable to auto-detect serial port. Pass it as the first argument." >&2
  exit 1
fi

echo "[erase] Erasing via esptool on $PORT"
esptool.py --chip esp32 --port "$PORT" erase_flash
