//
//  SupabaseModels.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 12/5/25.
//

import Foundation

// Matches health_readings table
struct HealthReadingRow: Codable {
    let id: Int
    let device_id: String?
    let heart_rate: Double?
    let temperature: Double?
    let steps: Int?
    let timestamp: Date?
    let epoch_min: Int?
}

// Matches health_summaries table
struct HealthSummaryRow: Codable {
    let id: Int
    let device_id: String?
    let total_steps: Int?
    let total_distance_miles: Double?
    let total_calories: Double?
    let record_count: Int?
    let start_timestamp: Date?
    let end_timestamp: Date?
    let upload_timestamp: Date?
    let height_inches: Double?
    let weight_lbs: Double?
    let created_at: Date?
}
