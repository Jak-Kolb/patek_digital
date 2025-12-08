//
//  DeviceConfiguration.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 10/14/25.
//

import Foundation

// Configuration settings for ESP32 device sent via GATT write operations
struct DeviceConfiguration: Codable {
    // Enable/disable push notifications for new readings
    var notificationsEnabled: Bool = true
    
    // Connection timeout in seconds (5-60s range)
    var connectionTimeout: Int = 30
    
    var isValid: Bool {
        return connectionTimeout >= 5 && connectionTimeout <= 60
    }
    
    // Encodes configuration into Data for GATT write
    // Format: [notifications(1 byte), timeout(1 byte)]
    func encode() -> Data {
        var data = Data()
        
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
        guard data.count >= 2 else { return nil }
        
        var config = DeviceConfiguration()
        
        // Extract notifications flag (byte 0)
        config.notificationsEnabled = data[0] == 1
        
        // Extract timeout (byte 1)
        config.connectionTimeout = Int(data[1])
        
        return config.isValid ? config : nil
    }
}
