#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

echo "[build] Compiling firmware..."
pio run

echo "[flash] Uploading to board..."
pio run -t upload
