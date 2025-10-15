//
//  HealthReading+CoreDataProperties.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 10/14/25.
//
//

import Foundation
import CoreData


extension HealthReading {

    @nonobjc public class func fetchRequest() -> NSFetchRequest<HealthReading> {
        return NSFetchRequest<HealthReading>(entityName: "HealthReading")
    }

    @NSManaged public var batteryLevel: Int16
    @NSManaged public var heartRate: Int16
    @NSManaged public var qualityFlags: String?
    @NSManaged public var stepCount: Int32
    @NSManaged public var temperature: Double
    @NSManaged public var timestamp: Date?

}

extension HealthReading : Identifiable {

}
