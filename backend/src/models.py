from pydantic import BaseModel

class DataPayload(BaseModel):
    data: list

class ResultPayload(BaseModel):
    result: float
