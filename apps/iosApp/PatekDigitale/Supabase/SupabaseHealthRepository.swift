//
//  SupabaseHealthRepository.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 12/5/25.
//

import Foundation
import Supabase

class SupabaseHealthRepository: ObservableObject {
    private let client = supabaseClient
    
    // Fetch recent health readings
    func fetchRecentReadings(deviceId: String? = nil, limit: Int = 100) async throws -> [HealthReadingRow] {
        if let deviceId = deviceId {
            let response: [HealthReadingRow] = try await client
                .from("health_readings")
                .select()
                .eq("device_id", value: deviceId)
                .order("timestamp", ascending: false)
                .limit(limit)
                .execute()
                .value
            return response
        } else {
            let response: [HealthReadingRow] = try await client
                .from("health_readings")
                .select()
                .order("timestamp", ascending: false)
                .limit(limit)
                .execute()
                .value
            return response
        }
    }
    
    // Fetch readings within a time range
    func fetchReadings(from startDate: Date, to endDate: Date, deviceId: String? = nil) async throws -> [HealthReadingRow] {
        if let deviceId = deviceId {
            let response: [HealthReadingRow] = try await client
                .from("health_readings")
                .select()
                .eq("device_id", value: deviceId)
                .gte("timestamp", value: startDate.ISO8601Format())
                .lte("timestamp", value: endDate.ISO8601Format())
                .order("timestamp", ascending: true)
                .execute()
                .value
            return response
        } else {
            let response: [HealthReadingRow] = try await client
                .from("health_readings")
                .select()
                .gte("timestamp", value: startDate.ISO8601Format())
                .lte("timestamp", value: endDate.ISO8601Format())
                .order("timestamp", ascending: true)
                .execute()
                .value
            return response
        }
    }
    
    // Fetch health summaries
    func fetchSummaries(deviceId: String? = nil, limit: Int = 30) async throws -> [HealthSummaryRow] {
        if let deviceId = deviceId {
            let response: [HealthSummaryRow] = try await client
                .from("health_summaries")
                .select()
                .eq("device_id", value: deviceId)
                .order("created_at", ascending: false)
                .limit(limit)
                .execute()
                .value
            return response
        } else {
            let response: [HealthSummaryRow] = try await client
                .from("health_summaries")
                .select()
                .order("created_at", ascending: false)
                .limit(limit)
                .execute()
                .value
            return response
        }
    }
    
    // Upload new readings
    func uploadReadings(_ readings: [HealthReadingInsert]) async throws {
        guard !readings.isEmpty else { return }
        
        // Supabase limits batch sizes, so we chunk it
        let chunkSize = 100
        let chunks = stride(from: 0, to: readings.count, by: chunkSize).map {
            Array(readings[$0..<min($0 + chunkSize, readings.count)])
        }
        
        for chunk in chunks {
            try await client
                .from("health_readings")
                .insert(chunk)
                .execute()
        }
        
        print("✅ Uploaded \(readings.count) readings to Supabase")
    }
    
    // Upload summary
    func uploadSummary(_ summary: HealthSummaryInsert) async throws {
        try await client
            .from("health_summaries")
            .insert(summary)
            .execute()
        print("✅ Uploaded summary to Supabase")
    }
}
