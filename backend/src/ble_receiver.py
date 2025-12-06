#!/usr/bin/env python3
# \"\"\"Ad-hoc BLE bridge for streaming ESP32 consolidated samples to the backend.\"\"\"

from __future__ import annotations

import argparse
import asyncio
import os
import struct
import time
from dataclasses import dataclass
from typing import List, Optional

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

from supabase_client import supabase

DEFAULT_DEVICE_NAME = os.environ.get("BLE_DEVICE_NAME", "ESP32-DataNode")
DEFAULT_DEVICE_ADDRESS = os.environ.get("BLE_DEVICE_ADDRESS")
DEFAULT_DEVICE_ID = os.environ.get("DEVICE_ID", "esp32-devkit")
DEFAULT_ERASE_AFTER = os.environ.get("ERASE_AFTER_UPLOAD", "false").lower() in ("true", "1", "yes")
DEFAULT_HEIGHT = float(os.environ.get("USER_HEIGHT_INCHES")) if os.environ.get("USER_HEIGHT_INCHES") else None
DEFAULT_WEIGHT = float(os.environ.get("USER_WEIGHT_LBS")) if os.environ.get("USER_WEIGHT_LBS") else None

SERVICE_UUID = "12345678-1234-5678-1234-56789abc0000"
DATA_CHAR_UUID = "12345678-1234-5678-1234-56789abc1001"
CONTROL_CHAR_UUID = "12345678-1234-5678-1234-56789abc1002"
DEFAULT_SERVICE_UUID = os.environ.get("BLE_SERVICE_UUID", SERVICE_UUID)

# Protocol Markers (must match C++ firmware)
START_MARKER = 0x01
DATA_MARKER = 0x02
END_MARKER = 0x03

# Struct format: <HhHI (uint16, int16, uint16, uint32)
# Matches: avg_hr_x10, avg_temp_x100, step_count, timestamp
RECORD_STRUCT = struct.Struct("<HhHI")


def log(message: str) -> None:
    print(f"[BLE] {message}")


@dataclass
class TransferState:
    expected: int = 0
    records: List[bytes] = None
    finished: asyncio.Event = None

    def __post_init__(self) -> None:
        self.records = []
        self.finished = asyncio.Event()

    def reset(self) -> None:
        self.expected = 0
        self.records.clear()
        self.finished.clear()


def decode_record(payload: bytes) -> dict:
    avg_hr_x10, avg_temp_x100, step_count, timestamp = RECORD_STRUCT.unpack(payload)
    return {
        "avg_hr_x10": int(avg_hr_x10),
        "avg_temp_x100": int(avg_temp_x100),
        "step_count": int(step_count),
        "timestamp": int(timestamp),
    }


async def discover_device(name: str, service_uuid: Optional[str], timeout: float = 10.0) -> Optional[str]:
    target_name = (name or "").strip().lower()
    log(f"Scanning for device '{name}'...")

    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    
    for d, adv in devices.values():
        dev_name = (d.name or "").lower()
        local_name = (adv.local_name or "").lower()
        uuids = [u.lower() for u in adv.service_uuids]

        if target_name in dev_name or target_name in local_name:
            log(f"Found device: {d.address} ({d.name})")
            return d.address
            
        if service_uuid and service_uuid.lower() in uuids:
            log(f"Found device by UUID: {d.address} ({d.name})")
            return d.address

    log("No matching device found.")
    return None


async def write_command(client: BleakClient, command: str, response: bool = True) -> None:
    await client.write_gatt_char(
        CONTROL_CHAR_UUID, 
        command.encode("ascii"), 
        response=response
    )


async def stream_records(client: BleakClient, state: TransferState, disconnect_event: asyncio.Event, sync_time: bool):
    state.reset()
    # Robust queue size
    queue: asyncio.Queue[bytes] = asyncio.Queue(maxsize=1024)
    loop = asyncio.get_running_loop()

    def handle_notification(sender, data: bytearray):
        try:
            loop.call_soon_threadsafe(queue.put_nowait, bytes(data))
        except asyncio.QueueFull:
            pass # Drop silently to keep loop moving

    async def consumer():
        while True:
            data = await queue.get()
            if not data: continue
            
            marker = data[0]
            payload = data[1:]

            if marker == START_MARKER:
                if len(payload) == 4:
                    state.expected = struct.unpack("<I", payload)[0]
                    log(f"Start marker received. Expecting {state.expected} records.")
            elif marker == DATA_MARKER:
                if len(payload) == RECORD_STRUCT.size:
                    state.records.append(payload)
            elif marker == END_MARKER:
                log("End marker received.")
                state.finished.set()

    await client.start_notify(DATA_CHAR_UUID, handle_notification)
    consumer_task = asyncio.create_task(consumer())

    if sync_time:
        log("Syncing time...")
        # response=True is safe for Time Sync
        await write_command(client, f"TIME:{int(time.time())}", response=True)
        await asyncio.sleep(0.5)

    log("Requesting records...")
    
    # --- IMPORTANT: Use response=False to prevent deadlock/disconnect ---
    # Make sure your write_command function supports the 'response' arg!
    await write_command(client, "SEND", response=False)

    # Wait for: Finish OR Disconnect OR Timeout
    done, pending = await asyncio.wait(
        [
            asyncio.create_task(state.finished.wait()), 
            asyncio.create_task(disconnect_event.wait())
        ],
        return_when=asyncio.FIRST_COMPLETED,
        timeout=60.0
    )

    # Cleanup
    consumer_task.cancel()
    
    if client.is_connected:
        try:
            await client.stop_notify(DATA_CHAR_UUID)
        except Exception:
            pass

    if disconnect_event.is_set():
        log("Transfer failed: Device disconnected mid-stream.")
        return []
    
    if not state.finished.is_set():
        log("Transfer timed out.")
        return []

    log(f"Transfer finished. Received {len(state.records)}/{state.expected} records.")
    return [decode_record(r) for r in state.records]



def calculate_calories_burned(steps: int, weight_lbs: float, height_inches: float) -> float:
    height_feet = height_inches / 12.0
    stride_length_feet = height_feet * 0.43
    distance_miles = (stride_length_feet * steps) / 5280.0
    calories_per_mile = weight_lbs * 0.57
    return distance_miles * calories_per_mile


async def upload_to_supabase(device_id: str, records: List[dict], height_inches: Optional[float] = None, weight_lbs: Optional[float] = None) -> dict:
    from datetime import datetime
    
    if not records:
        return {"status": "ok", "inserted": 0}
    
    total_steps = sum(rec['step_count'] for rec in records)
    total_distance_miles = (3.0 * total_steps) / 5280.0
    
    total_calories = None
    if height_inches and weight_lbs:
        total_calories = calculate_calories_burned(total_steps, weight_lbs, height_inches)
    
    timestamps = [rec['timestamp'] for rec in records]
    first_ts = datetime.fromtimestamp(min(timestamps))
    last_ts = datetime.fromtimestamp(max(timestamps))
    
    supabase_records = []
    for rec in records:
        supabase_records.append({
            'device_id': device_id,
            'heart_rate': rec['avg_hr_x10'] / 10.0,
            'temperature': rec['avg_temp_x100'] / 100.0,
            'steps': rec['step_count'],
            'timestamp': datetime.fromtimestamp(rec['timestamp']).isoformat(),
            'epoch_min': rec['timestamp'] // 60 # Keep for backward compatibility if needed, or remove
        })
    
    # Batch insert
    try:
        # Supabase might limit batch size, chunking is safer for large datasets
        chunk_size = 100
        for i in range(0, len(supabase_records), chunk_size):
            chunk = supabase_records[i:i + chunk_size]
            supabase.table('health_readings').insert(chunk).execute()
            
        log(f"Uploaded {len(supabase_records)} records to Supabase.")

        summary_record = {
            'device_id': device_id,
            'total_steps': total_steps,
            'total_distance_miles': round(total_distance_miles, 2),
            'total_calories': round(total_calories, 2) if total_calories else None,
            'record_count': len(records),
            'start_timestamp': first_ts.isoformat(),
            'end_timestamp': last_ts.isoformat(),
            'upload_timestamp': datetime.now().isoformat(),
            'height_inches': height_inches,
            'weight_lbs': weight_lbs
        }
        
        supabase.table('health_summaries').insert(summary_record).execute()
        log("Uploaded summary.")
        
        return {"status": "ok", "inserted": len(records)}
        
    except Exception as e:
        log(f"Supabase upload error: {e}")
        raise


async def erase_storage(client: BleakClient) -> None:
    log("Erasing storage...")
    await write_command(client, "ERASE")
    await asyncio.sleep(1.0)


async def run(args: argparse.Namespace) -> None:
    address = args.address
    if not address:
        address = await discover_device(args.name, args.service_uuid)
        if not address:
            return

    state = TransferState()
    
    # Create the event that tracks connection status
    disconnect_event = asyncio.Event()

    def on_disconnect(client):
        log("!!! DEVICE DISCONNECTED UNEXPECTEDLY !!!")
        disconnect_event.set()

    # PASS CALLBACK TO CONSTRUCTOR HERE
    async with BleakClient(address, disconnected_callback=on_disconnect) as client:
        if not client.is_connected:
            log("Failed to connect.")
            return

        log(f"Connected to {address}")

        # Pass the disconnect_event into stream_records so it can watch it
        records = await stream_records(
            client, 
            state, 
            disconnect_event, 
            sync_time=not args.skip_time_sync
        )
        
        if not records:
            log("No records received.")
            return

        if not args.skip_supabase:
            try:
                await upload_to_supabase(
                    args.device_id, 
                    records,
                    height_inches=args.height,
                    weight_lbs=args.weight
                )
            except Exception:
                if not args.continue_on_error:
                    return

        if args.erase_after and not args.no_erase:
            await erase_storage(client)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Simple BLE Bridge")
    parser.add_argument("--name", default=DEFAULT_DEVICE_NAME)
    parser.add_argument("--address", default=DEFAULT_DEVICE_ADDRESS)
    parser.add_argument("--device-id", default=DEFAULT_DEVICE_ID)
    parser.add_argument("--service-uuid", default=DEFAULT_SERVICE_UUID)
    parser.add_argument("--height", type=float, default=DEFAULT_HEIGHT)
    parser.add_argument("--weight", type=float, default=DEFAULT_WEIGHT)
    parser.add_argument("--skip-time-sync", action="store_true")
    parser.add_argument("--skip-supabase", action="store_true")
    parser.add_argument("--continue-on-error", action="store_true")
    parser.add_argument("--erase-after", action="store_true", default=DEFAULT_ERASE_AFTER)
    parser.add_argument("--no-erase", action="store_true", help="Do not erase storage after upload")
    return parser.parse_args()


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
