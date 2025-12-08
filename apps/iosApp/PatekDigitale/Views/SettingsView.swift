//
// SettingsView.swift
// PatekDigitale
//
// Created by vishalm3416 on 10/14/25.
//

import SwiftUI

struct SettingsView: View {
    // Reference to BLE manager for device communication
    @ObservedObject var bleManager: BLEManager
    
    // Local state for configuration UI
    @State private var notificationsEnabled: Bool
    @State private var connectionTimeout: Double
    
    @State private var showSaveAlert = false
    
    // User Stats Persistence
    @AppStorage("userHeightInches") private var userHeightInches: Double = 70.0
    @AppStorage("userWeightLbs") private var userWeightLbs: Double = 170.0
    
    init(bleManager: BLEManager) {
        self.bleManager = bleManager
        
        // Initialize state from current configuration
        let config = bleManager.configuration
        _notificationsEnabled = State(initialValue: config.notificationsEnabled)
        _connectionTimeout = State(initialValue: Double(config.connectionTimeout))
    }
    
    var body: some View {
        NavigationView {
            Form {
                // MARK: - Connection Section
                Section(header: Text("Connection")) {
                    // Device information
                    HStack {
                        Text("Device")
                        Spacer()
                        Text(bleManager.deviceName)
                            .foregroundColor(.secondary)
                    }
                    
                    // Connection status with indicator
                    HStack {
                        Text("Status")
                        Spacer()
                        Circle()
                            .fill(bleManager.isConnected ? Color.green : Color.red)
                            .frame(width: 12, height: 12)
                        Text(bleManager.isConnected ? "Connected" : "Disconnected")
                            .foregroundColor(.secondary)
                    }
                    
                    // Signal strength indicator (RSSI)
                    HStack {
                        Text("Signal Strength")
                        Spacer()
                        // Convert RSSI to visual bars
                        SignalStrengthView(rssi: bleManager.rssi)
                        Text("\(bleManager.rssi) dBm")
                            .foregroundColor(.secondary)
                            .font(.caption)
                    }
                    
                    // Manual control buttons
                    HStack {
                        // Scan button
                        Button(action: {
                            if bleManager.isScanning {
                                bleManager.stopScanning()
                            } else {
                                bleManager.startScanning()
                            }
                        }) {
                            HStack {
                                Image(systemName: bleManager.isScanning ? "stop.circle" : "antenna.radiowaves.left.and.right")
                                Text(bleManager.isScanning ? "Stop Scan" : "Scan")
                            }
                        }
                        .disabled(bleManager.isConnected)
                        
                        Spacer()
                        
                        // Connect/Disconnect button
                        Button(action: {
                            if bleManager.isConnected {
                                bleManager.disconnect()
                            } else {
                                bleManager.connect()
                            }
                        }) {
                            HStack {
                                Image(systemName: bleManager.isConnected ? "antenna.radiowaves.left.and.right.slash" : "antenna.radiowaves.left.and.right")
                                Text(bleManager.isConnected ? "Disconnect" : "Connect")
                            }
                        }
                        .disabled(bleManager.discoveredPeripheral == nil && !bleManager.isConnected)
                    }
                }
                
                // MARK: - User Profile Section
                Section(header: Text("User Profile")) {
                    HStack {
                        Text("Height (inches)")
                        Spacer()
                        TextField("Height", value: $userHeightInches, formatter: NumberFormatter())
                            .keyboardType(.decimalPad)
                            .multilineTextAlignment(.trailing)
                            .frame(width: 80)
                    }
                    
                    HStack {
                        Text("Weight (lbs)")
                        Spacer()
                        TextField("Weight", value: $userWeightLbs, formatter: NumberFormatter())
                            .keyboardType(.decimalPad)
                            .multilineTextAlignment(.trailing)
                            .frame(width: 80)
                    }
                    
                    // Display calculated stride length for info
                    HStack {
                        Text("Est. Stride Length")
                        Spacer()
                        let strideFt = (userHeightInches / 12.0) * 0.43
                        Text(String(format: "%.2f ft", strideFt))
                            .foregroundColor(.secondary)
                    }
                }
                
                // MARK: - Device Configuration Section
                Section(header: Text("Device Configuration"),
                       footer: Text("Configuration will be written to the device when you tap Save")) {
                    
                    // Connection timeout stepper (5-60 seconds)
                    VStack(alignment: .leading) {
                        HStack {
                            Text("Connection Timeout:")
                                .font(.headline)
                            Spacer()
                            Stepper("\(Int(connectionTimeout))s",
                                   value: $connectionTimeout,
                                   in: 5...60,
                                   step: 5)
                        }
                        Text("Time to wait before timing out connection attempts.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    .padding(.vertical, 4)
                    
                    // Notifications toggle
                    Toggle(isOn: $notificationsEnabled) {
                        VStack(alignment: .leading) {
                            Text("Push Notifications")
                                .font(.headline)
                            Text("Enable BLE notifications for new readings")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                    }
                    
                    // Save button
                    Button(action: saveConfiguration) {
                        HStack {
                            Spacer()
                            Image(systemName: "square.and.arrow.down")
                            Text("Save to Device")
                            Spacer()
                        }
                    }
                    .disabled(!bleManager.isConnected)
                }
                
                // MARK: - Data Quality Section
                Section(header: Text("Data Quality")) {
                    HStack {
                        Text("Current Quality")
                        Spacer()
                        QualityIndicator(flags: bleManager.latestQualityFlags)
                    }
                    
                    Text(bleManager.latestQualityFlags.description)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                
                // MARK: - Advanced Settings Section
                Section(header: Text("Advanced")) {
                    // Read configuration from device
                    Button(action: {
                        bleManager.readConfiguration()
                    }) {
                        HStack {
                            Image(systemName: "arrow.down.circle")
                            Text("Read from Device")
                        }
                    }
                    .disabled(!bleManager.isConnected)
                }
            }
            .navigationTitle("Settings")
            .alert("Configuration Saved", isPresented: $showSaveAlert) {
                Button("OK", role: .cancel) { }
            } message: {
                Text("Your configuration has been written to the device.")
            }
        }
    }
    
    // MARK: - Helper Functions
    
    // Saves current UI state to device configuration
    private func saveConfiguration() {
        var config = DeviceConfiguration()
        config.notificationsEnabled = notificationsEnabled
        config.connectionTimeout = Int(connectionTimeout)
        
        // Validate and write to device
        if config.isValid {
            bleManager.writeConfiguration(config)
            showSaveAlert = true
        }
    }
}

// MARK: - Supporting Views

// Preview for SwiftUI canvas
struct SettingsView_Previews: PreviewProvider {
    static var previews: some View {
        SettingsView(bleManager: BLEManager())
    }
}
