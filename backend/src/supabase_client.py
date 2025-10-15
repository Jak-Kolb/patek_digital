from supabase import create_client
from app.config import SUPABASE_URL, SUPABASE_KEY

supabase = create_client(SUPABASE_URL, SUPABASE_KEY)

def store_result(result):
    data = {"result": result}
    supabase.table("results").insert(data).execute()
