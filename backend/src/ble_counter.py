#!/usr/bin/env python3
import os, time, asyncio
from bleak import BleakClient, BleakScanner

SERVICE_UUID      = os.environ.get("BLE_SERVICE_UUID", "12345678-1234-5678-1234-56789abc0000").lower()
DATA_CHAR_UUID    = os.environ.get("DATA_CHAR_UUID",   "12345678-1234-5678-1234-56789abc1001").lower()
CONTROL_CHAR_UUID = os.environ.get("CONTROL_CHAR_UUID","12345678-1234-5678-1234-56789abc1002").lower()
DEVICE_NAME       = os.environ.get("BLE_DEVICE_NAME",  "ESP32-DataNode")

START, DATA, END, ACK = map(ord, "CDEA")

async def find_address():
    print(f"[TEST] Scanning for '{DEVICE_NAME}' or service {SERVICE_UUID} …")
    devices = await BleakScanner.discover(timeout=8.0, return_adv=True)
    if isinstance(devices, dict):
        items = devices.items()
    else:
        items = [(d, None) for d in devices]
    for dev, adv in items:
        addr = getattr(dev, "address", None) if dev else None
        name = getattr(dev, "name", None) if dev else None
        uuids = set([u.lower() for u in getattr(adv, "service_uuids", [])]) if adv else set()
        if (name and DEVICE_NAME.lower() in (name or "").lower()) or (SERVICE_UUID in uuids):
            print(f"[TEST] Using {addr} name={name} uuids={list(uuids) or '<none>'}")
            return addr
    return None

async def main():
    addr = None
    if os.path.exists(".esp32_uuid"):
        addr = open(".esp32_uuid").read().strip()
    if not addr:
        addr = await find_address()
        if not addr:
            print("[TEST] No device found"); return

    expected = None
    total_notifies = 0
    data_notifies = 0
    done = asyncio.Event()
    loop = asyncio.get_running_loop()
    q: asyncio.Queue[bytes] = asyncio.Queue()

    def on_notify(_: int, data: bytearray):
        if data:
            loop.call_soon_threadsafe(q.put_nowait, bytes(data))

    async def consumer():
        nonlocal expected, total_notifies, data_notifies
        while True:
            data = await q.get()
            try:
                total_notifies += 1
                m = data[0]
                if m == START:
                    try: expected = int(data[1:].decode("ascii"))
                    except: expected = 0
                    print(f"[TEST] #{total_notifies:03d} MARKER=C expected={expected}")
                elif m == DATA:
                    data_notifies += 1
                    # compact progress print (doesn't spam too hard)
                    print(f"[TEST] #{total_notifies:03d} MARKER=D data_count={data_notifies}")
                    if expected and data_notifies >= expected:
                        print(f"[TEST] reached expected={expected}")
                        done.set()
                elif m == END:
                    print(f"[TEST] #{total_notifies:03d} MARKER=E END")
                    done.set()
                elif m == ACK:
                    print(f"[TEST] #{total_notifies:03d} MARKER=A ACK")
                else:
                    print(f"[TEST] #{total_notifies:03d} MARKER=0x{m:02X} len={len(data)}")
            finally:
                q.task_done()

    t0 = time.time()
    async with BleakClient(addr) as client:
        await client.start_notify(DATA_CHAR_UUID, on_notify)
        consumer_task = asyncio.create_task(consumer())

        # Time sync: write-with-response (macOS prefers this)
        await client.write_gatt_char(CONTROL_CHAR_UUID, f"TIME:{int(time.time())}".encode(), response=True)
        await asyncio.sleep(0.2)

        # Ask to stream. (If ESP32 already streaming, we still finish by count.)
        await client.write_gatt_char(CONTROL_CHAR_UUID, b"SEND", response=True)

        try:
            await asyncio.wait_for(done.wait(), timeout=30.0)
        except asyncio.TimeoutError:
            print(f"[TEST] TIMEOUT: data_notifies={data_notifies} expected={expected} total_notifies={total_notifies}")

        await client.stop_notify(DATA_CHAR_UUID)
        try:
            await asyncio.wait_for(q.join(), timeout=1.0)
        except asyncio.TimeoutError:
            pass
        consumer_task.cancel()

    dt = time.time() - t0
    print(f"[TEST] FINAL: data_notifies={data_notifies} expected={expected} total_notifies={total_notifies} in {dt:.2f}s")

if __name__ == "__main__":
    # BLEAK_LOGGING=debug python ble_counter.py  (for verbose CoreBluetooth logs)
    asyncio.run(main())
