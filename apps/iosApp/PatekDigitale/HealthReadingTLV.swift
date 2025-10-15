//
//  HealthReadingTLV.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 10/14/25.
//

import Foundation


// Type-Length-Value health reading from ESP32
struct HealthReadingTLV: Codable {
    let type: UInt8      // Type identifier (e.g., heart rate, step count, etc.)
    let length: UInt8    // Length of the value
    let value: Data      // Raw value data

    func decodedValue() -> Any? {
        switch type { // Replace with appropriate ESP32 registers
        case 0x01: // Heart Rate
            return value.withUnsafeBytes { $0.load(as: Int16.self) }
        case 0x02: // Step Count
            return value.withUnsafeBytes { $0.load(as: Int32.self) }
        case 0x03: // Temperature
            return value.withUnsafeBytes { $0.load(as: Double.self) }
        case 0x04: // Battery Level
            return value.withUnsafeBytes { $0.load(as: Int16.self) }
        default:
            return nil
        }
    }
}
