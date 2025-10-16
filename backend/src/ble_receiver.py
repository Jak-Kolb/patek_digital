"""
BLE receiver using Bleak to talk to the ESP32 NimBLE peripheral.

Flow:
- Scan for device by name or service UUID
- Connect
- Subscribe to data characteristic notifications
- Optionally write a control command (e.g., "SEND") to trigger transfer
- Collect notifications until timeout or disconnect

Note: On macOS, Bleak uses CoreBluetooth; run with appropriate permissions.
"""
from __future__ import annotations

import asyncio
from typing import List, Optional

from bleak import BleakClient, BleakScanner

from config import (
    BLE_CONTROL_CHAR_UUID,
    BLE_DATA_CHAR_UUID,
    BLE_DEVICE_NAME,
    BLE_SERVICE_UUID,
)


async def _find_device_address(timeout: float = 10.0) -> Optional[str]:
    devices = await BleakScanner.discover(timeout=timeout)
    for d in devices:
        if d.name == BLE_DEVICE_NAME:
            return d.address
        # As a fallback, check advertised services
        if hasattr(d, "metadata"):
            svc_uuids = (d.metadata or {}).get("uuids", [])
            if BLE_SERVICE_UUID.lower() in [u.lower() for u in svc_uuids or []]:
                return d.address
    return None


async def receive_ble_data_async(
    command: str = "SEND",
    notify_timeout: float = 5.0,
    max_wait: float = 20.0,
) -> bytes:
    """
    Connects to the ESP32 and collects notification payloads.

    Args:
        command: Text written to control characteristic to start transfer.
        notify_timeout: Seconds of silence after which we stop collecting.
        max_wait: Absolute max seconds to wait before returning.

    Returns:
        Concatenated bytes received via notifications.
    """
    address = await _find_device_address()
    if not address:
        raise RuntimeError("BLE device not found. Ensure ESP32 is advertising and nearby.")

    chunks: List[bytes] = []

    def _on_notify(_: int, data: bytearray):
        chunks.append(bytes(data))

    async with BleakClient(address, timeout=10.0) as client:
        if not client.is_connected:
            raise RuntimeError("Failed to connect to BLE device")

        # Subscribe to notifications first, then trigger SEND to avoid losing initial chunk
        await client.start_notify(BLE_DATA_CHAR_UUID, _on_notify)

        # Write command to control characteristic if provided
        if command:
            await client.write_gatt_char(BLE_CONTROL_CHAR_UUID, command.encode("utf-8"), response=True)

        # Wait for data with inactivity timeout
        total_wait = 0.0
        last_count = 0
        while total_wait < max_wait:
            await asyncio.sleep(0.5)
            total_wait += 0.5
            if len(chunks) != last_count:
                # Reset inactivity timer when new data arrives
                last_count = len(chunks)
                total_wait = 0.0
            if total_wait >= notify_timeout and len(chunks) > 0:
                break

        await client.stop_notify(BLE_DATA_CHAR_UUID)

    return b"".join(chunks)


def receive_ble_data(command: str = "SEND", notify_timeout: float = 5.0, max_wait: float = 20.0) -> bytes:
    """Synchronous wrapper used by FastAPI path handlers or scripts."""
    return asyncio.run(receive_ble_data_async(command=command, notify_timeout=notify_timeout, max_wait=max_wait))

