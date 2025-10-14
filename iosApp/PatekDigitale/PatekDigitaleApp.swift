//
//  PatekDigitaleApp.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 10/13/25.
//

import SwiftUI

@main
struct PatekDigitaleApp: App {
    // Shared isntance of data controller
    @StateObject private var dataController = DataController.shared

    var body: some Scene {
        WindowGroup {
            ContentView()
            
            // Makes CoreData context available to all views
            .environment(\.managedObjectContext, dataController.container.viewContext)
        }
    }
}
