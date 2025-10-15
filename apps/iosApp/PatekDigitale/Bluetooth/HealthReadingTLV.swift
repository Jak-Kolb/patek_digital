//
// HealthReadingTLV.swift
// PatekDigitale
//
// Created by vishalm3416 on 10/14/25.
//

import Foundation

// Quality flags for health reading reliability assessment
struct QualityFlags: OptionSet {
    let rawValue: UInt8
    
    // Individual quality indicators
    static let lowSignal = QualityFlags(rawValue: 1 << 0)      // Bit 0: Weak sensor signal
    static let highMotion = QualityFlags(rawValue: 1 << 1)     // Bit 1: Excessive movement detected
    static let lowPerfusion = QualityFlags(rawValue: 1 << 2)   // Bit 2: Poor blood flow/sensor contact
    static let sensorError = QualityFlags(rawValue: 1 << 3)    // Bit 3: Hardware sensor malfunction
    static let batteryLow = QualityFlags(rawValue: 1 << 4)     // Bit 4: Device battery critically low
    
    // Computed property to check if data is reliable
    var isReliable: Bool {
        return !self.contains(.sensorError) && !self.contains(.lowPerfusion)
    }
    
    // Human-readable description of quality issues
    var description: String {
        var issues: [String] = []
        if contains(.lowSignal) { issues.append("Weak Signal") }
        if contains(.highMotion) { issues.append("High Motion") }
        if contains(.lowPerfusion) { issues.append("Low Perfusion") }
        if contains(.sensorError) { issues.append("Sensor Error") }
        if contains(.batteryLow) { issues.append("Low Battery") }
        return issues.isEmpty ? "Good" : issues.joined(separator: ", ")
    }
}

// Type-Length-Value health reading from ESP32
struct HealthReadingTLV: Codable {
    let type: UInt8
    let length: UInt8
    let value: Data
    
    // Decodes the raw value based on type identifier
    func decodedValue() -> Any? {
        guard value.count >= Int(length) else { return nil }
        
        switch type {
        case 0x01: // Heart Rate (16-bit integer, 0-255 bpm)
            return value.withUnsafeBytes { $0.load(as: Int16.self) }
            
        case 0x02: // Step Count (32-bit integer, accumulated steps)
            return value.withUnsafeBytes { $0.load(as: Int32.self) }
            
        case 0x03: // Temperature (64-bit double, degrees Fahrenheit)
            return value.withUnsafeBytes { $0.load(as: Double.self) }
            
        case 0x04: // Battery Level (16-bit integer, 0-100%)
            return value.withUnsafeBytes { $0.load(as: Int16.self) }
            
        case 0x05: // Quality Flags (8-bit bitmask)
            return value.withUnsafeBytes { $0.load(as: UInt8.self) }
            
        default:
            return nil
        }
    }
}

// Efficient TLV parser for ESP32 BLE notifications
class TLVParser {
    static func parse(_ data: Data) -> [HealthReadingTLV] {
        var readings: [HealthReadingTLV] = []
        var offset = 0
        
        while offset + 2 <= data.count { // Need at least Type + Length bytes
            let type = data[offset]
            let length = data[offset + 1]
            
            offset += 2
            
            // Validate we have enough remaining bytes for the value
            guard offset + Int(length) <= data.count else {
                print("TLV parsing error: insufficient data for value")
                break
            }
            
            let value = data.subdata(in: offset..<offset + Int(length))
            
            let tlv = HealthReadingTLV(type: type, length: length, value: value)
            readings.append(tlv)
            
            offset += Int(length)
        }
        
        return readings
    }
    
    // Batches multiple TLV readings into a single CoreData transaction
    static func batchProcess(_ readings: [HealthReadingTLV],
                            timestamp: Date,
                            repository: HealthRepository) {
 
        var heartRate: Int16?
        var stepCount: Int32?
        var temperature: Double?
        var batteryLevel: Int16?
        var qualityFlags: UInt8 = 0
        
        for tlv in readings {
            switch tlv.type {
            case 0x01: heartRate = tlv.decodedValue() as? Int16
            case 0x02: stepCount = tlv.decodedValue() as? Int32
            case 0x03: temperature = tlv.decodedValue() as? Double
            case 0x04: batteryLevel = tlv.decodedValue() as? Int16
            case 0x05: qualityFlags = tlv.decodedValue() as? UInt8 ?? 0
            default: break
            }
        }
        
        // Save as single CoreData entry
        repository.saveBatchReading(
            timestamp: timestamp,
            heartRate: heartRate ?? 0,
            stepCount: stepCount ?? 0,
            temperature: temperature ?? 0.0,
            batteryLevel: batteryLevel ?? 0,
            qualityFlags: QualityFlags(rawValue: qualityFlags)
        )
    }
}
