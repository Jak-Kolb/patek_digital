#!/usr/bin/env python3
"""Find ESP32 BLE device and get its UUID."""

import asyncio
import sys
from pathlib import Path
from bleak import BleakScanner

SERVICE_UUID = "12345678-1234-5678-1234-56789abc0000"
found_esp32 = None

def detection_callback(device, advertisement_data):
    global found_esp32
    name = advertisement_data.local_name or device.name
    
    if name and ('esp32' in name.lower() or 'datanode' in name.lower()):
        if not found_esp32:  # Only print once
            found_esp32 = device.address
            print(f"✓ Found ESP32: {device.address}")

async def scan():
    scanner = BleakScanner(detection_callback=detection_callback)
    await scanner.start()
    await asyncio.sleep(5.0)
    await scanner.stop()
    
    if found_esp32:
        # Save UUID
        uuid_file = Path(__file__).parent.parent / ".esp32_uuid"
        uuid_file.write_text(found_esp32)
        return 0
    
    print("❌ ESP32 not found - make sure it's powered on")
    return 1

if __name__ == "__main__":
    sys.exit(asyncio.run(scan()))
