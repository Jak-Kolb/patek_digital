import os
from dotenv import load_dotenv

load_dotenv(dotenv_path="../.env")

# Supabase configuration
SUPABASE_URL = os.getenv("SUPABASE_URL")
# Prefer service role key when available, fall back to anon or legacy SUPABASE_KEY
SUPABASE_KEY = (
	os.getenv("SUPABASE_SERVICE_KEY")
	or os.getenv("SUPABASE_ANON_KEY")
	or os.getenv("SUPABASE_KEY")
)

# BLE configuration (defaults mirror firmware app_config.h)
BLE_DEVICE_NAME = os.getenv("BLE_DEVICE_NAME", "ESP32-DataNode")
BLE_SERVICE_UUID = os.getenv("BLE_SERVICE_UUID", "12345678-1234-5678-1234-56789abc0000")
BLE_DATA_CHAR_UUID = os.getenv("BLE_DATA_CHAR_UUID", "12345678-1234-5678-1234-56789abc1001")
BLE_CONTROL_CHAR_UUID = os.getenv("BLE_CONTROL_CHAR_UUID", "12345678-1234-5678-1234-56789abc1002")

