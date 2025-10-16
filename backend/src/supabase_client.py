from typing import Any, Dict

from supabase import create_client

from config import SUPABASE_KEY, SUPABASE_URL



if not SUPABASE_URL or not SUPABASE_KEY:
    raise RuntimeError(
        "Missing Supabase configuration. Ensure SUPABASE_URL and SUPABASE_ANON_KEY or SUPABASE_SERVICE_KEY are set."
    )

supabase = create_client(SUPABASE_URL, SUPABASE_KEY)


def store_result(result: Any) -> Dict[str, Any]:
    data = {"result": result}
    response = supabase.table("results").insert(data).execute()
    return {"status": "ok", "inserted": response.data}
