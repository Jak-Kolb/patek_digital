//
//  DataController.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 10/13/25.
//

import Foundation
import CoreData

// Manages the CoreData stack
class DataController: ObservableObject {
    // Manages the CoreData model and database file
    let container: NSPersistentContainer

    // Shared singleton instance for easy access throughout the app
    static let shared = DataController()

    private init() {
        container = NSPersistentContainer(name: "HealthModel")
        container.loadPersistentStores { storeDescription, error in
            if let error = error {
                fatalError("Unresolved error loading CoreData store: \(error)")
            }
        }
    }

    // Provides the main context for reading/writing data
    var context: NSManagedObjectContext {
        container.viewContext
    }

    // Saves changes in the context to the persistent store
    func save() {
        let context = container.viewContext
        if context.hasChanges {
            do {
                try context.save()
            } catch {
                print("Error saving CoreData context: \(error)")
            }
        }
    }
}
