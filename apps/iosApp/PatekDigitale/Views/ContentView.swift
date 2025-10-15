//
// ContentView.swift
// PatekDigitale
//
// Created by vishalm3416 on 10/13/25.
//

import SwiftUI
import CoreData

struct ContentView: View {
    @Environment(\.managedObjectContext) private var context
    
    // Fetch HealthReading objects from CoreData, sorted by most recent
    @FetchRequest(
        sortDescriptors: [NSSortDescriptor(keyPath: \HealthReading.timestamp, ascending: false)],
        animation: .default
    )
    private var readings: FetchedResults<HealthReading>
    
    // BLE manager instance
    @StateObject var bleManager = BLEManager()
    
    // Show settings sheet
    @State private var showSettings = false
    
    var body: some View {
        NavigationView {
            List {
                // MARK: - Connection Status Section
                Section(header: Text("Connection")) {
                    HStack {
                        // Connection indicator
                        Circle()
                            .fill(bleManager.isConnected ? Color.green : Color.red)
                            .frame(width: 12, height: 12)
                        
                        Text(bleManager.isConnected ? "Connected" : "Disconnected")
                        
                        Spacer()
                        
                        // Device name
                        Text(bleManager.deviceName)
                            .foregroundColor(.secondary)
                            .font(.caption)
                        
                        // Signal strength
                        if bleManager.isConnected {
                            SignalStrengthView(rssi: bleManager.rssi)
                        }
                    }
                }
                
                // MARK: - Latest Health Data Section
                Section(header: HStack {
                    Text("Latest Reading")
                    Spacer()
                    // Quality indicator
                    QualityIndicator(flags: bleManager.latestQualityFlags)
                }) {
                    if let latest = readings.first {
                        // Heart Rate
                        HStack {
                            Image(systemName: "heart.fill")
                                .foregroundColor(.red)
                            Text("Heart Rate")
                            Spacer()
                            Text("\(latest.heartRate) bpm")
                                .foregroundColor(.secondary)
                        }
                        
                        // Step Count
                        HStack {
                            Image(systemName: "figure.walk")
                                .foregroundColor(.blue)
                            Text("Steps")
                            Spacer()
                            Text("\(latest.stepCount)")
                                .foregroundColor(.secondary)
                        }
                        
                        // Battery Level
                        HStack {
                            Image(systemName: batteryIcon(level: latest.batteryLevel))
                                .foregroundColor(batteryColor(level: latest.batteryLevel))
                            Text("Battery")
                            Spacer()
                            Text("\(latest.batteryLevel)%")
                                .foregroundColor(.secondary)
                        }
                        
                        // Temperature
                        HStack {
                            Image(systemName: "thermometer")
                                .foregroundColor(.orange)
                            Text("Temperature")
                            Spacer()
                            Text(String(format: "%.1f°F", latest.temperature))
                                .foregroundColor(.secondary)
                        }
                        
                        // Quality Flags
                        HStack {
                            Image(systemName: "info.circle")
                                .foregroundColor(.gray)
                            Text("Quality")
                            Spacer()
                            Text(latest.qualityFlags ?? "Good")
                                .foregroundColor(.secondary)
                        }
                        
                        // Timestamp
                        HStack {
                            Image(systemName: "clock")
                                .foregroundColor(.gray)
                            Text("Timestamp")
                            Spacer()
                            Text(latest.timestamp?.formatted(date: .omitted, time: .shortened) ?? "-")
                                .foregroundColor(.secondary)
                        }
                    } else {
                        // No data available
                        Text("No readings available")
                            .foregroundColor(.secondary)
                    }
                }
                
                // MARK: - Recent History Section
                Section(header: Text("Recent History")) {
                    ForEach(readings.prefix(10)) { reading in
                        VStack(alignment: .leading, spacing: 4) {
                            // Reading summary
                            HStack {
                                Text("♥️ \(reading.heartRate) bpm")
                                Text("•")
                                Text("🚶 \(reading.stepCount) steps")
                                Spacer()
                                Text("🔋 \(reading.batteryLevel)%")
                            }
                            .font(.subheadline)
                            
                            // Timestamp and quality
                            HStack {
                                Text(reading.timestamp?.formatted(date: .abbreviated, time: .shortened) ?? "-")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                                Spacer()
                                Text(reading.qualityFlags ?? "Good")
                                    .font(.caption)
                                    .foregroundColor(reading.qualityFlags == "Good" ? .green : .orange)
                            }
                        }
                        .padding(.vertical, 4)
                    }
                }
            }
            .navigationTitle("Health Monitor")
            .toolbar {
                // Settings button in navigation bar
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(action: {
                        showSettings = true
                    }) {
                        Image(systemName: "gear")
                    }
                }
            }
            .sheet(isPresented: $showSettings) {
                // Present settings view as modal sheet
                SettingsView(bleManager: bleManager)
            }
        }
        .onAppear {
            // Initialize BLE manager with CoreData context
            bleManager.setContext(context)
        }
    }
    
    // MARK: - Helper Functions
    
    // Returns appropriate battery icon based on level
    private func batteryIcon(level: Int16) -> String {
        switch level {
        case 75...100: return "battery.100"
        case 50..<75: return "battery.75"
        case 25..<50: return "battery.50"
        case 10..<25: return "battery.25"
        default: return "battery.0"
        }
    }
    
    // Returns color for battery level
    private func batteryColor(level: Int16) -> Color {
        switch level {
        case 50...100: return .green
        case 20..<50: return .orange
        default: return .red
        }
    }
}

// Preview for SwiftUI canvas
struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
            .environment(\.managedObjectContext, DataController.shared.container.viewContext)
    }
}
