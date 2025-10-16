#!/usr/bin/env python3
"""
Simplified BLE downloader - just gets data and prints it, no backend upload.
"""
import asyncio
import base64
import struct
import sys
import time
from datetime import datetime
from bleak import BleakClient

# UUIDs
SERVICE_UUID = "12345678-1234-5678-1234-56789abc0000"
DATA_CHAR_UUID = "12345678-1234-5678-1234-56789abc1001"
CONTROL_CHAR_UUID = "12345678-1234-5678-1234-56789abc1002"

# Protocol markers
START_MARKER = ord('C')
DATA_MARKER = ord('D')
END_MARKER = ord('E')
ACK_MARKER = ord('A')

# Record format: <HhHI = avg_hr_x10, avg_temp_x100, step_count, epoch_min
RECORD_STRUCT = struct.Struct("<HhHI")


class DownloadState:
    def __init__(self):
        self.records = []
        self.expected = None
        self.finished = asyncio.Event()
    
    def reset(self, expected=None):
        self.records = []
        self.expected = expected
        self.finished.clear()


async def download_data(address: str) -> list:
    """Connect to ESP32 and download all records."""
    state = DownloadState()
    disconnected = False
    
    def disconnected_callback(client):
        nonlocal disconnected
        disconnected = True
        print("\n⚠️  BLE connection lost!")
    
    async def handle_notification(_: int, data: bytearray) -> None:
        if not data:
            return
        
        marker = data[0]
        
        if marker == START_MARKER:
            try:
                count = int(data[1:].decode("ascii"))
                state.reset(count)
                print(f"📡 ESP32 announcing {count} records...")
            except ValueError:
                print("⚠️  Invalid start marker")
        
        elif marker == DATA_MARKER:
            raw = base64.b64decode(bytes(data[1:]))
            if len(raw) == RECORD_STRUCT.size:
                state.records.append(raw)
                print(f"📥 Received record {len(state.records)}/{state.expected or '?'}", end='\r')
            else:
                print(f"\n⚠️  Malformed record (length {len(raw)})")
        
        elif marker == END_MARKER:
            print("\n✓ Transfer complete")
            state.finished.set()
        
        elif marker == ACK_MARKER:
            ack = data[1:].decode("ascii", errors="ignore")
            print(f"✓ ACK: {ack}")
    
    print(f"🔌 Connecting to {address}...")
    async with BleakClient(address, timeout=20.0, disconnected_callback=disconnected_callback) as client:
        print("✓ Connected")
        print(f"   MTU: {client.mtu_size} bytes")
        
        # Start notifications
        await client.start_notify(DATA_CHAR_UUID, handle_notification)
        
        # Sync time
        print(f"⏰ Syncing time...")
        current_time = int(time.time())
        await client.write_gatt_char(CONTROL_CHAR_UUID, f"TIME:{current_time}".encode())
        await asyncio.sleep(0.5)
        
        # Request data
        print("📤 Requesting data...")
        await client.write_gatt_char(CONTROL_CHAR_UUID, b"SEND")
        
        # Wait for transfer (with generous timeout)
        try:
            await asyncio.wait_for(state.finished.wait(), timeout=10.0)
        except asyncio.TimeoutError:
            print(f"\n⚠️  Timeout after {len(state.records)} records")
            if len(state.records) < (state.expected or 0):
                print(f"   Expected {state.expected}, got {len(state.records)}")
            if disconnected:
                print(f"   Connection was lost during transfer")
        
        await client.stop_notify(DATA_CHAR_UUID)
        
        if not disconnected and not client.is_connected:
            print("⚠️  Client shows disconnected")
        
        return state.records


def decode_record(raw: bytes) -> dict:
    """Decode a raw record into readable format."""
    avg_hr_x10, avg_temp_x100, step_count, epoch_min = RECORD_STRUCT.unpack(raw)
    return {
        'avg_hr_x10': avg_hr_x10,
        'avg_temp_x100': avg_temp_x100,
        'step_count': step_count,
        'epoch_min': epoch_min,
    }


def print_records(records: list):
    """Print records in a nice table format."""
    if not records:
        print("\n❌ No records received")
        return
    
    decoded = [decode_record(raw) for raw in records]
    
    print("\n" + "=" * 85)
    print(f"📊 DOWNLOADED {len(decoded)} HEALTH RECORDS FROM ESP32")
    print("=" * 85)
    print(f"{'#':>3} | {'Heart Rate':>12} | {'Temperature':>12} | {'Steps':>6} | {'Timestamp'}")
    print("-" * 85)
    
    for i, rec in enumerate(decoded, 1):
        hr = rec['avg_hr_x10'] / 10.0
        temp = rec['avg_temp_x100'] / 100.0
        steps = rec['step_count']
        epoch_min = rec['epoch_min']
        
        # Convert epoch minutes to readable time
        dt = datetime.fromtimestamp(epoch_min * 60)
        time_str = dt.strftime('%Y-%m-%d %H:%M')
        
        print(f"{i:3d} | {hr:8.1f} bpm | {temp:8.2f}°C | {steps:6d} | {time_str}")
    
    print("=" * 85)
    
    # Summary stats
    hrs = [rec['avg_hr_x10'] / 10.0 for rec in decoded]
    temps = [rec['avg_temp_x100'] / 100.0 for rec in decoded]
    total_steps = sum(rec['step_count'] for rec in decoded)
    
    print(f"\n📈 Summary:")
    print(f"   Avg HR: {sum(hrs)/len(hrs):.1f} bpm (range: {min(hrs):.1f}-{max(hrs):.1f})")
    print(f"   Avg Temp: {sum(temps)/len(temps):.2f}°C (range: {min(temps):.2f}-{max(temps):.2f})")
    print(f"   Total Steps: {total_steps}")
    print()


async def main():
    if len(sys.argv) < 2:
        print("Usage: python ble_download.py <ESP32_ADDRESS>")
        print("\nTip: Run 'python src/active_scan.py' first to find the address")
        print("     Then: python src/ble_download.py \"$(cat .esp32_uuid)\"")
        sys.exit(1)
    
    address = sys.argv[1]
    
    try:
        records = await download_data(address)
        print_records(records)
    except Exception as e:
        print(f"\n❌ Error: {e}")
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
