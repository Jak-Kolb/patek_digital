#!/usr/bin/env python3
"""Clear all data from Supabase health tables."""

from supabase_client import supabase

def clear_tables():
    """Delete all records from health_readings and health_summaries tables."""
    
    # Delete from health_readings
    print("Deleting all records from health_readings...")
    result1 = supabase.table('health_readings').delete().neq('id', 0).execute()
    print(f"✓ Deleted records from health_readings")
    
    # Delete from health_summaries
    print("Deleting all records from health_summaries...")
    result2 = supabase.table('health_summaries').delete().neq('id', 0).execute()
    print(f"✓ Deleted records from health_summaries")
    
    print("\n✓ All tables cleared successfully!")

if __name__ == "__main__":
    confirm = input("⚠️  This will delete ALL data from health_readings and health_summaries. Continue? (yes/no): ")
    if confirm.lower() in ['yes', 'y']:
        clear_tables()
    else:
        print("Cancelled.")
