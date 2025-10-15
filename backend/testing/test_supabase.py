import os
from supabase import create_client
from dotenv import load_dotenv

load_dotenv(dotenv_path="../.env")

url = os.environ.get("SUPABASE_URL")
key = os.environ.get("SUPABASE_ANON_KEY")
supabase = create_client(url, key)

data_to_insert = {
    "result": 123.45,           # Example numeric value
    "description": "test run",  # Add this column in Supabase if you want to store text
    "status": "ok"              # Add this column in Supabase if you want to store status
}
insert_response = supabase.table("results").insert(data_to_insert).execute()
print("Insert response:", insert_response)

# Fetch all rows to verify
select_response = supabase.table("results").select("*").execute()
print("Select response:", select_response)
