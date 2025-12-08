//
// HealthRepository.swift
// PatekDigitale
//
// Created by vishalm3416 on 10/14/25.
//

import Foundation
import CoreData

// Repository for HealthReading CoreData operations
class HealthRepository {
    private let context: NSManagedObjectContext
    
    init(context: NSManagedObjectContext) {
        self.context = context
    }
    
    
    // Optimized batch save for multiple TLV values in one transaction
    func saveBatchReading(timestamp: Date,
                         heartRate: Int16,
                         stepCount: Int32,
                         temperature: Double,
                         batteryLevel: Int16,
                         qualityFlags: QualityFlags) {

        context.perform {
            let reading = HealthReading(context: self.context)
            reading.timestamp = timestamp
            reading.heartRate = heartRate
            reading.stepCount = stepCount
            reading.temperature = temperature
            reading.batteryLevel = batteryLevel
            reading.qualityFlags = qualityFlags.description 
            
            do {
                try self.context.save()
            } catch {
                print("Error saving batch reading: \(error)")
            }
        }
    }
    
    // Fetches all readings sorted by most recent first
    func fetchAllReadings() -> [HealthReading] {
        let request: NSFetchRequest<HealthReading> = HealthReading.fetchRequest()
        request.sortDescriptors = [NSSortDescriptor(keyPath: \HealthReading.timestamp, ascending: false)]
        
        do {
            return try context.fetch(request)
        } catch {
            print("Error fetching readings: \(error)")
            return []
        }
    }
    
    // Deletes readings older than specified date (for 7-day retention policy)
    func deleteOldReadings(before date: Date) {
        let request: NSFetchRequest<NSFetchRequestResult> = HealthReading.fetchRequest()
        request.predicate = NSPredicate(format: "timestamp < %@", date as NSDate)
        
        let deleteRequest = NSBatchDeleteRequest(fetchRequest: request)
        
        do {
            try context.execute(deleteRequest)
            try context.save()
        } catch {
            print("Error deleting old readings: \(error)")
        }
    }
    
    /// Deletes ALL health readings from CoreData
    func deleteAllReadings() {
        let request = NSFetchRequest<NSFetchRequestResult>(entityName: "HealthReading")
        let deleteRequest = NSBatchDeleteRequest(fetchRequest: request)
        
        do {
            try context.execute(deleteRequest)
            try context.save()
            print("✅ Deleted all readings from CoreData")
        } catch {
            print("❌ Error deleting all readings: \(error)")
        }
    }

}
