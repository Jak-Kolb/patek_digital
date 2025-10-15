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
    @State private var samplingInterval: Double
    @State private var burstLength: Double
    @State private var notificationsEnabled: Bool
    @State private var connectionTimeout: Double
    
    @State private var showSaveAlert = false
    
    init(bleManager: BLEManager) {
        self.bleManager = bleManager
        
        // Initialize state from current configuration
        let config = bleManager.configuration
        _samplingInterval = State(initialValue: Double(config.samplingInterval))
        _burstLength = State(initialValue: Double(config.heartRateBurstLength))
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
                
                // MARK: - Device Configuration Section
                Section(header: Text("Device Configuration"),
                       footer: Text("Configuration will be written to the device when you tap Save")) {
                    
                    // Sampling interval slider (100-5000ms)
                    VStack(alignment: .leading) {
                        Text("Sampling Interval: \(Int(samplingInterval))ms")
                            .font(.headline)
                        Slider(value: $samplingInterval, in: 100...5000, step: 100)
                        Text("How often the device takes readings. Lower = more frequent updates but higher battery drain.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    .padding(.vertical, 4)
                    
                    // Heart rate burst length stepper (1-10 samples)
                    VStack(alignment: .leading) {
                        HStack {
                            Text("HR Burst Length:")
                                .font(.headline)
                            Spacer()
                            Stepper("\(Int(burstLength)) samples",
                                   value: $burstLength,
                                   in: 1...10,
                                   step: 1)
                        }
                        Text("Number of consecutive heart rate readings to average for accuracy.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    .padding(.vertical, 4)
                    
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
                    // Mock data toggle for testing
                    Toggle(isOn: $bleManager.useMockData) {
                        VStack(alignment: .leading) {
                            Text("Mock Data Mode")
                                .font(.headline)
                            Text("Generate simulated data for testing")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                    }
                    .onChange(of: bleManager.useMockData) {
                        if bleManager.useMockData {
                            bleManager.startMockDataGeneration()
                        } else {
                            bleManager.stopMockDataGeneration()
                        }
                    }
                    
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
        config.samplingInterval = Int(samplingInterval)
        config.heartRateBurstLength = Int(burstLength)
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

// Visual signal strength indicator (bars)
struct SignalStrengthView: View {
    let rssi: Int
    
    // Convert RSSI to 0-4 bar scale
    private var barCount: Int {
        switch rssi {
        case -50...0: return 4    // Excellent
        case -60..<(-50): return 3 // Good
        case -70..<(-60): return 2 // Fair
        case -80..<(-70): return 1 // Poor
        default: return 0          // Very poor
        }
    }
    
    var body: some View {
        HStack(spacing: 2) {
            ForEach(0..<4) { index in
                RoundedRectangle(cornerRadius: 2)
                    .fill(index < barCount ? Color.green : Color.gray.opacity(0.3))
                    .frame(width: 4, height: CGFloat(4 + index * 3))
            }
        }
    }
}

// Quality indicator with color-coded icon
struct QualityIndicator: View {
    let flags: QualityFlags
    
    var body: some View {
        HStack {
            Image(systemName: flags.isReliable ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                .foregroundColor(flags.isReliable ? .green : .orange)
            Text(flags.isReliable ? "Good" : "Issues")
                .foregroundColor(.secondary)
        }
    }
}

// Preview for SwiftUI canvas
struct SettingsView_Previews: PreviewProvider {
    static var previews: some View {
        SettingsView(bleManager: BLEManager())
    }
}
