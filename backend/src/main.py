from fastapi import FastAPI, Request
from app.processor import process_data
from app.supabase_client import store_result
from app.models import DataPayload, ResultPayload

app = FastAPI()

@app.post("/data")
async def receive_data(payload: DataPayload):
    result = process_data(payload.data)
    store_result(result)
    return ResultPayload(result=result)

