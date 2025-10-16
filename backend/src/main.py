from fastapi import FastAPI
from processor import process_data
from supabase_client import store_result
from models import DataPayload, ResultPayload
from ble_receiver import receive_ble_data

app = FastAPI()

@app.post("/data")
async def receive_data(payload: DataPayload):
    result = process_data(payload.data)
    store_result(result)
    return ResultPayload(result=result)


@app.post("/collect_ble")
async def collect_ble():
    # Pull binary payload from ESP32. You can parse it here or in processor
    raw = receive_ble_data(command="SEND")
    # Example: interpret as list of ints for demo processing
    processed = process_data(list(raw))
    store_result(processed)
    return {"bytes": len(raw), "result": processed}

@app.get("/health")
async def health():
    return {"status": "ok"}

