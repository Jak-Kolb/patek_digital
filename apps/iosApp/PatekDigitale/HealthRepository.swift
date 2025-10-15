//
//  HealthRepository.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 10/14/25.
//

import Foundation
import CoreData

// Repository for HealthReading CoreData operations
class HealthRepository {
    private let context: NSManagedObjectContext

    init(context: NSManagedObjectContext) {
        self.context = context
    }

    // Saves a new HealthReading from BLE data
    func saveReading(from tlv: HealthReadingTLV, timestamp: Date, qualityFlags: String = "") {
        context.perform {
            let reading = HealthReading(context: self.context)
            reading.timestamp = timestamp

            // Decode and assign values based on TLV type
            switch tlv.type {
            case 0x01: reading.heartRate = tlv.decodedValue() as? Int16 ?? 0
            case 0x02: reading.stepCount = tlv.decodedValue() as? Int32 ?? 0
            case 0x03: reading.temperature = tlv.decodedValue() as? Double ?? 0.0
            case 0x04: reading.batteryLevel = tlv.decodedValue() as? Int16 ?? 0
            default: break
            }
            reading.qualityFlags = qualityFlags

            do {
                try self.context.save()
            } catch {
                print("Error saving HealthReading: \(error)")
            }
        }
    }

    // Fetches readings within a date range.
    func fetchReadings(from start: Date, to end: Date) -> [HealthReading] {
        let request: NSFetchRequest<HealthReading> = HealthReading.fetchRequest()
        request.predicate = NSPredicate(format: "timestamp >= %@ AND timestamp <= %@", start as NSDate, end as NSDate)
        request.sortDescriptors = [NSSortDescriptor(key: "timestamp", ascending: false)]
        do {
            return try context.fetch(request)
        } catch {
            print("Error fetching readings: \(error)")
            return []
        }
    }

    // Cleans up readings older than 7 days.
    func cleanupOldReadings() {
        let sevenDaysAgo = Calendar.current.date(byAdding: .day, value: -7, to: Date())!
        let request: NSFetchRequest<HealthReading> = HealthReading.fetchRequest()
        request.predicate = NSPredicate(format: "timestamp < %@", sevenDaysAgo as NSDate)
        do {
            let oldReadings = try context.fetch(request)
            for reading in oldReadings {
                context.delete(reading)
            }
            try context.save()
        } catch {
            print("Error cleaning up old readings: \(error)")
        }
    }
}
