//
//  SupabaseClient.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 12/5/25.
//

import Foundation
import Supabase

let supabaseClient = SupabaseClient(
    supabaseURL: URL(string: "https://ahzdtlqdsbsdjcygksxz.supabase.co")!,
    supabaseKey: "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImFoemR0bHFkc2JzZGpjeWdrc3h6Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjA1MzU2NDksImV4cCI6MjA3NjExMTY0OX0.VnHS68Vti6bzNMzexUdGeKlhsk2d7tvu2jMHMFzSnsM"
)
