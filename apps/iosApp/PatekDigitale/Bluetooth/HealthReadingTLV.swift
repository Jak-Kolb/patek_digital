//
// HealthReadingTLV.swift
// PatekDigitale
//
// Updated to match Firmware/Python protocol
//

import Foundation

// Structure matching the firmware's consolidated record
struct FirmwareRecord {
    let heartRate: Double
    let temperature: Double
    let stepCount: Int
    let timestamp: Date
}

class BLEProtocolParser {
    // Protocol Markers
    static let START_MARKER: UInt8 = 0x01
    static let DATA_MARKER: UInt8 = 0x02
    static let END_MARKER: UInt8   = 0x03
    
    // Struct size: 2 (HR) + 2 (Temp) + 2 (Steps) + 4 (Time) = 10 bytes
    static let RECORD_SIZE = 10
    
    static func parseRecord(_ data: Data) -> FirmwareRecord? {
        // Data payload starts after the marker byte, so we expect exactly RECORD_SIZE bytes passed here
        guard data.count >= RECORD_SIZE else { return nil }
        
        // Parse Little Endian values
        // 1. Heart Rate (UInt16) - avg_hr_x10
        let hrRaw = data.subdata(in: 0..<2).withUnsafeBytes { $0.load(as: UInt16.self) }
        
        // 2. Temperature (Int16) - avg_temp_x100
        let tempRaw = data.subdata(in: 2..<4).withUnsafeBytes { $0.load(as: Int16.self) }
        
        // 3. Step Count (UInt16)
        let stepsRaw = data.subdata(in: 4..<6).withUnsafeBytes { $0.load(as: UInt16.self) }
        
        // 4. Timestamp (UInt32)
        let timeRaw = data.subdata(in: 6..<10).withUnsafeBytes { $0.load(as: UInt32.self) }
        
        return FirmwareRecord(
            heartRate: Double(hrRaw) / 10.0,
            temperature: Double(tempRaw) / 100.0,
            stepCount: Int(stepsRaw),
            timestamp: Date(timeIntervalSince1970: TimeInterval(timeRaw))
        )
    }
}

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
