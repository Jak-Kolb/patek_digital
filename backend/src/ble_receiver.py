#!/usr/bin/env python3
"""Ad-hoc BLE bridge for streaming ESP32 consolidated samples to the backend."""

from __future__ import annotations

import argparse
import asyncio
import base64
import os
import struct
import time
from dataclasses import dataclass
from typing import List, Optional

import httpx
from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

from supabase_client import supabase

DEFAULT_DEVICE_NAME = os.environ.get("BLE_DEVICE_NAME", "ESP32-DataNode")
DEFAULT_DEVICE_ADDRESS = os.environ.get("BLE_DEVICE_ADDRESS")
DEFAULT_BACKEND_URL = os.environ.get("BACKEND_URL", "")  # Empty string = disabled
DEFAULT_DEVICE_ID = os.environ.get("DEVICE_ID", "esp32-devkit")
DEFAULT_ERASE_AFTER = os.environ.get("ERASE_AFTER_UPLOAD", "false").lower() in ("true", "1", "yes")
DEFAULT_HEIGHT = float(os.environ.get("USER_HEIGHT_INCHES")) if os.environ.get("USER_HEIGHT_INCHES") else None
DEFAULT_WEIGHT = float(os.environ.get("USER_WEIGHT_LBS")) if os.environ.get("USER_WEIGHT_LBS") else None

SERVICE_UUID = "12345678-1234-5678-1234-56789abc0000"
DATA_CHAR_UUID = "12345678-1234-5678-1234-56789abc1001"
CONTROL_CHAR_UUID = "12345678-1234-5678-1234-56789abc1002"
DEFAULT_SERVICE_UUID = os.environ.get("BLE_SERVICE_UUID", SERVICE_UUID)

START_MARKER = ord("C")
DATA_MARKER = ord("D")
END_MARKER = ord("E")
ACK_MARKER = ord("A")

RECORD_STRUCT = struct.Struct("<HhHI")


def log(message: str) -> None:
    print(f"[BLE] {message}")


@dataclass
class TransferState:
    expected: Optional[int] = None
    records: List[bytes] = None
    finished: asyncio.Event = None
    last_ack: Optional[str] = None

    def __post_init__(self) -> None:
        self.records = []
        self.finished = asyncio.Event()

    def reset(self, expected: int = 0) -> None:
        self.expected = expected
        self.records.clear()
        self.finished.clear()
        self.last_ack = None


def decode_record(payload: bytes) -> dict:
    avg_hr_x10, avg_temp_x100, step_count, epoch_min = RECORD_STRUCT.unpack(payload)
    return {
        "avg_hr_x10": int(avg_hr_x10),
        "avg_temp_x100": int(avg_temp_x100),
        "step_count": int(step_count),
        "epoch_min": int(epoch_min),
    }


async def discover_device(name: str, service_uuid: Optional[str], timeout: float = 12.0) -> Optional[str]:
    target_name = (name or "").strip().lower()
    target_service = (service_uuid or "").strip().lower()
    log(f"Scanning for name='{name}' service='{service_uuid or 'any'}'…")

    # Discover devices with advertisement data
    devices = await BleakScanner.discover(timeout=timeout, return_adv=True)
    
    matches: List[str] = []
    discovered_list = []
    
    # Handle both dict and list return types from BleakScanner
    if isinstance(devices, dict):
        device_items = devices.items()
    else:
        device_items = [(d, None) for d in devices]
    
    for device_info in device_items:
        # Unpack device and advertisement data
        if isinstance(device_info, tuple) and len(device_info) == 2:
            device, adv_data = device_info
        else:
            device = device_info
            adv_data = None
        
        # Get device address (handle both BLEDevice objects and strings)
        if isinstance(device, BLEDevice):
            address = device.address
            device_name = device.name
        elif isinstance(device, str):
            # On macOS, device is sometimes just a UUID string
            address = device
            device_name = None
        else:
            log(f"Skipping unexpected device type: {type(device)}")
            continue
        
        # Extract device name from various sources
        names = []
        if device_name:
            names.append(device_name)
        if adv_data:
            local_name = getattr(adv_data, "local_name", None)
            if local_name:
                names.append(local_name)
        
        # Extract service UUIDs
        services = []
        if adv_data:
            adv_services = getattr(adv_data, "service_uuids", None)
            if adv_services:
                services.extend([uuid.lower() for uuid in adv_services])
        
        # Store for logging
        discovered_list.append((address, names, services))
        
        # Check for name match
        normalized_names = [n.lower() for n in names if n]
        name_match = target_name and any(target_name in n for n in normalized_names)
        
        # Check for service UUID match
        service_match = target_service and target_service in services
        
        if name_match or service_match:
            log(f"Found candidate {address} with names={names} services={services}")
            matches.append(address)
            if name_match and (not target_service or service_match):
                return address
    
    if matches:
        log("Multiple candidates found; using first match")
        return matches[0]
    
    if not discovered_list:
        log("No BLE advertisements observed during scan.")
    else:
        log(f"Discovered {len(discovered_list)} devices:")
        for address, names, services in discovered_list[:10]:  # Show first 10
            log(f"  {address} names={names or ['<unknown>']} services={services or ['<none>']}")
        if len(discovered_list) > 10:
            log(f"  ... and {len(discovered_list) - 10} more devices")
    
    log("No matching device found – confirm the ESP32 name/UUID or pass --address explicitly.")
    return None


async def write_command(client: BleakClient, command: str) -> None:
    await client.write_gatt_char(CONTROL_CHAR_UUID, command.encode("ascii"), response=True)


async def stream_records(client: BleakClient, state: TransferState, sync_time: bool) -> List[dict]:
    state.reset()
    
    # Latency tracking
    transfer_start_time = None
    first_packet_time = None
    last_packet_time = None
    packet_times = []

    def handle_notification(_: int, data: bytearray) -> None:
        nonlocal first_packet_time, last_packet_time
        packet_recv_time = time.time()
        
        if not data:
            return
        marker = data[0]
        if marker == START_MARKER:
            try:
                count = int(data[1:].decode("ascii"))
            except ValueError:
                count = 0
            state.reset(count)
            log(f"Streaming announced: {count} records")

        elif marker == DATA_MARKER:
            if first_packet_time is None:
                first_packet_time = packet_recv_time
            last_packet_time = packet_recv_time
            packet_times.append(packet_recv_time)

            # Still decoding base64 from ESP32; keep this unless you switch to raw
            raw = base64.b64decode(bytes(data[1:]))
            if len(raw) == RECORD_STRUCT.size:
                state.records.append(raw)
                log(f"Received record #{len(state.records)}")

                if state.expected is not None and state.expected > 0 and len(state.records) >= state.expected:
                    log("Reached expected record count; finishing")
                    state.finished.set()
            else:
                log(f"Skipping malformed record length {len(raw)}")

        elif marker == END_MARKER:
            log("End marker received")
            state.finished.set()

        elif marker == ACK_MARKER:
            state.last_ack = data[1:].decode("ascii", errors="ignore")
            log(f"ACK: {state.last_ack}")

        else:
            log(f"Unhandled packet: {data!r}")

    await client.start_notify(DATA_CHAR_UUID, handle_notification)

    if sync_time:
        await write_command(client, f"TIME:{int(time.time())}")
        await asyncio.sleep(0.5)

    transfer_start_time = time.time()
    await write_command(client, "SEND")

    # Adaptive timeout: base 10s + 250ms/record
    base_timeout = 10.0
    per_record_timeout = 0.25
    timeout = base_timeout + (per_record_timeout * (state.expected or 20))
    
    try:
        await asyncio.wait_for(state.finished.wait(), timeout=timeout)
    except asyncio.TimeoutError:
        log(f"Timed out after {timeout:.1f}s waiting for transfer to finish")
        state.finished.set()

    transfer_end_time = time.time()
    await client.stop_notify(DATA_CHAR_UUID)

    if state.expected is not None and state.records and len(state.records) != state.expected:
        log(f"Warning: expected {state.expected} records, received {len(state.records)}")

    # Latency / throughput stats (🔧 use 12 bytes/record)
    if packet_times and len(packet_times) > 1:
        total_transfer_time = transfer_end_time - transfer_start_time
        first_packet_latency = first_packet_time - transfer_start_time if first_packet_time else 0
        inter_packet_delays = [packet_times[i] - packet_times[i-1] for i in range(1, len(packet_times))]
        avg_inter_packet = sum(inter_packet_delays) / len(inter_packet_delays) if inter_packet_delays else 0
        min_inter_packet = min(inter_packet_delays) if inter_packet_delays else 0
        max_inter_packet = max(inter_packet_delays) if inter_packet_delays else 0

        # 🔧 Corrected: 12 bytes per record (your struct is <HhHI)
        throughput_bytes = len(state.records) * RECORD_STRUCT.size
        throughput_bps = (throughput_bytes * 8) / total_transfer_time if total_transfer_time > 0 else 0

        print("\n" + "=" * 80)
        print("BLE TRANSFER LATENCY STATISTICS")
        print("=" * 80)
        print(f"Total transfer time:      {total_transfer_time:.3f} seconds")
        print(f"First packet latency:     {first_packet_latency:.3f} seconds")
        print(f"Packets received:         {len(packet_times)}")
        print(f"Avg inter-packet delay:   {avg_inter_packet*1000:.2f} ms")
        print(f"Min inter-packet delay:   {min_inter_packet*1000:.2f} ms")
        print(f"Max inter-packet delay:   {max_inter_packet*1000:.2f} ms")
        print(f"Throughput:               {throughput_bytes/total_transfer_time:.2f} bytes/sec")
        print(f"                          {throughput_bps:.2f} bits/sec")
        print(f"Records per second:       {len(packet_times)/total_transfer_time:.2f}")
        print("=" * 80 + "\n")

    decoded_records = [decode_record(raw) for raw in state.records]
    # ... (rest of your function unchanged)
    if decoded_records:
        print("\n" + "=" * 80)
        print(f"RECEIVED {len(decoded_records)} RECORDS FROM ESP32")
        print("=" * 80)
        for i, rec in enumerate(decoded_records, 1):
            hr = rec['avg_hr_x10'] / 10.0
            temp = rec['avg_temp_x100'] / 100.0
            steps = rec['step_count']
            epoch_min = rec['epoch_min']
            
            # Convert epoch minutes to readable time
            from datetime import datetime
            dt = datetime.fromtimestamp(epoch_min * 60)
            time_str = dt.strftime('%Y-%m-%d %H:%M')
            
            print(f"Record {i:2d}: HR={hr:5.1f} bpm | Temp={temp:5.2f}°C | Steps={steps:4d} | Time={time_str}")
        print("=" * 80 + "\n")
        
    return decoded_records




def calculate_calories_burned(steps: int, weight_lbs: float, height_inches: float) -> float:
    """
    Calculate calories burned based on steps, weight, and height.
    Uses a simplified formula: calories = steps * (weight_lbs * 0.57) / 2000
    This is an approximation; more accurate formulas consider stride length and pace.
    """
    # Stride length estimation based on height (in feet)
    height_feet = height_inches / 12.0
    stride_length_feet = height_feet * 0.43  # Rough approximation
    
    # Distance in miles
    distance_miles = (stride_length_feet * steps) / 5280.0
    
    # Calories per mile varies by weight; rough formula is 0.57 * weight
    calories_per_mile = weight_lbs * 0.57
    total_calories = distance_miles * calories_per_mile
    
    return total_calories


async def upload_to_supabase(device_id: str, records: List[dict], height_inches: Optional[float] = None, weight_lbs: Optional[float] = None) -> dict:
    """Upload health records and aggregate metrics to Supabase database."""
    from datetime import datetime
    
    if not records:
        return {"status": "ok", "inserted": 0, "message": "No records to upload"}
    
    # Calculate aggregate metrics
    total_steps = sum(rec['step_count'] for rec in records)
    total_distance_miles = (3.0 * total_steps) / 5280.0  # Using 3 feet per step as specified
    
    # Calculate calories if height and weight are provided
    total_calories = None
    if height_inches is not None and weight_lbs is not None:
        total_calories = calculate_calories_burned(total_steps, weight_lbs, height_inches)
        log(f"Calculated {total_calories:.2f} calories burned (weight={weight_lbs}lbs, height={height_inches}in)")
    
    # Get timestamp range
    first_timestamp = datetime.fromtimestamp(min(rec['epoch_min'] for rec in records) * 60)
    last_timestamp = datetime.fromtimestamp(max(rec['epoch_min'] for rec in records) * 60)
    
    # Format individual records for Supabase
    supabase_records = []
    for rec in records:
        # Convert scaled values to actual values
        hr = rec['avg_hr_x10'] / 10.0
        temp = rec['avg_temp_x100'] / 100.0
        steps = rec['step_count']
        epoch_min = rec['epoch_min']
        
        # Convert epoch minutes to timestamp
        timestamp = datetime.fromtimestamp(epoch_min * 60)
        
        supabase_records.append({
            'device_id': device_id,
            'heart_rate': hr,
            'temperature': temp,
            'steps': steps,
            'timestamp': timestamp.isoformat(),
            'epoch_min': epoch_min
        })
    
    # Insert individual records into Supabase
    response = supabase.table('health_readings').insert(supabase_records).execute()
    log(f"Uploaded {len(supabase_records)} individual records to Supabase")
    
    # Create aggregate/summary record
    summary_record = {
        'device_id': device_id,
        'total_steps': total_steps,
        'total_distance_miles': round(total_distance_miles, 2),
        'total_calories': round(total_calories, 2) if total_calories is not None else None,
        'record_count': len(records),
        'start_timestamp': first_timestamp.isoformat(),
        'end_timestamp': last_timestamp.isoformat(),
        'upload_timestamp': datetime.now().isoformat(),
        'height_inches': height_inches,
        'weight_lbs': weight_lbs
    }
    
    # Insert summary into Supabase (adjust table name as needed)
    summary_response = supabase.table('health_summaries').insert(summary_record).execute()
    log(f"Uploaded summary: {total_steps} steps, {total_distance_miles:.2f} miles" + 
        (f", {total_calories:.2f} calories" if total_calories else ""))
    
    return {
        "status": "ok",
        "inserted_records": len(supabase_records),
        "summary": summary_record,
        "records_data": response.data,
        "summary_data": summary_response.data
    }


async def push_to_backend(url: str, device_id: str, records: List[dict]) -> dict:
    payload = b"".join(
        RECORD_STRUCT.pack(
            rec["avg_hr_x10"],
            rec["avg_temp_x100"],
            rec["step_count"],
            rec["epoch_min"],
        )
        for rec in records
    )
    payload_b64 = base64.b64encode(payload).decode("ascii")

    async with httpx.AsyncClient(timeout=30.0) as client:
        response = await client.post(
            url,
            json={
                "deviceId": device_id,
                "payloadBase64": payload_b64,
                "ts": time.time(),
            },
        )
        response.raise_for_status()
        return response.json()


async def erase_storage(client: BleakClient) -> None:
    await write_command(client, "ERASE")
    await asyncio.sleep(1.0)


async def run(args: argparse.Namespace) -> None:
    address = args.address
    if address is None:
        address = await discover_device(args.name, args.service_uuid)
        if address is None:
            return

    state = TransferState()

    async with BleakClient(address) as client:
        if not client.is_connected:
            raise RuntimeError("BLE connection failed")

        log("Connected to ESP32")

        records = await stream_records(client, state, sync_time=not args.skip_time_sync)
        if not records:
            log("No records received – nothing to upload.")
            return

        # Upload to Supabase
        if not args.skip_supabase:
            try:
                log(f"Uploading {len(records)} records to Supabase")
                supabase_response = await upload_to_supabase(
                    args.device_id, 
                    records,
                    height_inches=args.height,
                    weight_lbs=args.weight
                )
                log(f"Supabase response: {supabase_response}")
            except Exception as e:
                log(f"Failed to upload to Supabase: {e}")
                if not args.continue_on_error:
                    raise

        # Upload to HTTP backend
        if args.backend:
            try:
                log(f"Posting {len(records)} records to backend {args.backend}")
                backend_response = await push_to_backend(args.backend, args.device_id, records)
                log(f"Backend response: {backend_response}")
            except Exception as e:
                log(f"Failed to upload to HTTP backend: {e}")
                log("Continuing anyway (backend upload is optional)")
                if not args.continue_on_error:
                    log("Note: Use --continue-on-error to prevent this from stopping execution")

        if args.erase_after:
            log("Sending ERASE command")
            await erase_storage(client)
        else:
            log("Skipping ERASE (use --erase-after to enable)")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Stream consolidated records from ESP32 to backend")
    parser.add_argument("--name", default=DEFAULT_DEVICE_NAME, help=f"BLE device name to search for (default: {DEFAULT_DEVICE_NAME})")
    parser.add_argument("--address", default=DEFAULT_DEVICE_ADDRESS, help="BLE MAC/address (skip discovery)")
    parser.add_argument("--backend", default=DEFAULT_BACKEND_URL, help=f"Backend ingest endpoint (default: {'disabled' if not DEFAULT_BACKEND_URL else DEFAULT_BACKEND_URL})")
    parser.add_argument("--device-id", default=DEFAULT_DEVICE_ID, help=f"Device identifier for backend payloads (default: {DEFAULT_DEVICE_ID})")
    parser.add_argument(
        "--service-uuid",
        default=DEFAULT_SERVICE_UUID,
        help="BLE service UUID filter (set empty string to disable)",
    )
    parser.add_argument("--height", type=float, default=DEFAULT_HEIGHT, help=f"User height in inches (for calorie calculation){f' (default: {DEFAULT_HEIGHT})' if DEFAULT_HEIGHT else ''}")
    parser.add_argument("--weight", type=float, default=DEFAULT_WEIGHT, help=f"User weight in pounds (for calorie calculation){f' (default: {DEFAULT_WEIGHT})' if DEFAULT_WEIGHT else ''}")
    parser.add_argument("--skip-time-sync", action="store_true", help="Do not send TIME:<epoch> before transfer")
    parser.add_argument("--skip-supabase", action="store_true", help="Skip uploading to Supabase")
    parser.add_argument("--continue-on-error", action="store_true", help="Continue even if Supabase upload fails")
    parser.add_argument("--erase-after", action="store_true", default=DEFAULT_ERASE_AFTER, help=f"Send ERASE after successful upload (default: {DEFAULT_ERASE_AFTER})")
    parser.add_argument("--no-erase", dest="erase_after", action="store_false", help="Do not erase after upload (overrides --erase-after)")
    return parser.parse_args()


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
