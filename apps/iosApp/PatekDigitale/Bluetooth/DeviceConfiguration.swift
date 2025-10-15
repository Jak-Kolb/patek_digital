//
//  DeviceConfiguration.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 10/14/25.
//

import Foundation

// Configuration settings for ESP32 device sent via GATT write operations
struct DeviceConfiguration: Codable {
    // Sampling interval in milliseconds
    var samplingInterval: Int = 1000
    
    // Heart rate burst length in samples (1-10 range)
    // Number of consecutive HR readings to average for accuracy
    var heartRateBurstLength: Int = 3
    
    // Enable/disable push notifications for new readings
    var notificationsEnabled: Bool = true
    
    // Connection timeout in seconds (5-60s range)
    var connectionTimeout: Int = 30
    
    var isValid: Bool {
        return samplingInterval >= 100 && samplingInterval <= 5000 &&
               heartRateBurstLength >= 1 && heartRateBurstLength <= 10 &&
               connectionTimeout >= 5 && connectionTimeout <= 60
    }
    
    // Encodes configuration into Data for GATT write
    // Format: [samplingInterval(4 bytes), burstLength(1 byte), notifications(1 byte), timeout(1 byte)]
    func encode() -> Data {
        var data = Data()
        
        // Sampling interval as 32-bit integer (little-endian)
        var interval = UInt32(samplingInterval).littleEndian
        data.append(Data(bytes: &interval, count: 4))
        
        // Burst length as 8-bit integer
        var burst = UInt8(heartRateBurstLength)
        data.append(Data(bytes: &burst, count: 1))
        
        // Notifications flag as 8-bit boolean (0 or 1)
        var notif = UInt8(notificationsEnabled ? 1 : 0)
        data.append(Data(bytes: &notif, count: 1))
        
        // Timeout as 8-bit integer
        var timeout = UInt8(connectionTimeout)
        data.append(Data(bytes: &timeout, count: 1))
        
        return data
    }
    
    // Decodes configuration from Data received via GATT read
    static func decode(from data: Data) -> DeviceConfiguration? {
        guard data.count >= 7 else { return nil }
        
        var config = DeviceConfiguration()
        
        // Extract sampling interval (bytes 0-3)
        let interval = data.subdata(in: 0..<4).withUnsafeBytes { $0.load(as: UInt32.self) }
        config.samplingInterval = Int(interval)
        
        // Extract burst length (byte 4)
        config.heartRateBurstLength = Int(data[4])
        
        // Extract notifications flag (byte 5)
        config.notificationsEnabled = data[5] == 1
        
        // Extract timeout (byte 6)
        config.connectionTimeout = Int(data[6])
        
        return config.isValid ? config : nil
    }
}
