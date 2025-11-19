#!/usr/bin/env python3
"""Simple BLE scanner to find your ESP32 device."""

import asyncio
from bleak import BleakScanner

async def scan():
    print("Scanning for BLE devices (15 seconds)...")
    print("=" * 80)
    
    devices = await BleakScanner.discover(timeout=15.0, return_adv=True)
    
    if isinstance(devices, dict):
        device_list = list(devices.items())
    else:
        device_list = [(d, None) for d in devices]
    
    print(f"\nFound {len(device_list)} devices:\n")
    
    for item in device_list:
        if isinstance(item, tuple):
            device, adv_data = item
        else:
            device, adv_data = item, None
        
        # Handle both BLEDevice objects and strings
        if hasattr(device, 'address'):
            address = device.address
            name = device.name or "<no name>"
        else:
            address = str(device)
            name = "<unknown>"
        
        # Get local name from advertisement data
        if adv_data and hasattr(adv_data, 'local_name') and adv_data.local_name:
            name = adv_data.local_name
        
        # Get service UUIDs
        services = []
        if adv_data and hasattr(adv_data, 'service_uuids'):
            services = adv_data.service_uuids or []
        
        # Print device info
        print(f"Address: {address}")
        print(f"  Name: {name}")
        if services:
            print(f"  Services: {services}")
        
        # Highlight ESP32-like devices
        if "ESP32" in name or "DataNode" in name:
            print("  ⭐ THIS MIGHT BE YOUR DEVICE!")
        
        print()
    
    print("=" * 80)
    print("\nTo connect to a specific device, use:")
    print("  python src/ble_receiver.py --address <ADDRESS> --erase-after")

if __name__ == "__main__":
    asyncio.run(scan())
