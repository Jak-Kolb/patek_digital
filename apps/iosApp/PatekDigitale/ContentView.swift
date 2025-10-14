//
//  ContentView.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 10/13/25.
//

import SwiftUI
import CoreData

struct ContentView: View {
    @Environment(\.managedObjectContext) private var context
    
    // Fetch HealthReading objects from CoreData
    @FetchRequest(
        sortDescriptors: [NSSortDescriptor(keyPath: \HealthReading.timestamp, ascending: false)],
        animation: .default
    )
    private var readings: FetchedResults<HealthReading>
    
    @StateObject var bleManager = BLEManager()
    
    var body: some View {
        NavigationView {
            List {
                Section(header: Text("Connection")) {
                    Text("Status: \(bleManager.isConnected ? "Connected" : "Disconnected")")
                }
                
                Section(header: Text("Health Data")) {
                    if let latest = readings.first {
                        Text("Heart Rate: \(latest.heartRate) bpm")
                        Text("Steps: \(latest.stepCount)")
                        Text("Battery: \(latest.batteryLevel)%")
                        Text("Temperature: \(String(format: "%.1f", latest.temperature))°F")
                        Text("Quality Flags: \(latest.qualityFlags ?? "-")")
                        Text("Timestamp: \(latest.timestamp?.formatted() ?? "-")")
                    } else {
                        Text("No health data available.")
                    }
                }
            }
            .navigationTitle("Patek Digitale")
            
            .onAppear { bleManager.context = context }
        }
    }
}
