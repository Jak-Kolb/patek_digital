//
//  EnhancedSettingsView.swift
//  PatekDigitale
//
//  Enhanced settings with notifications, device info, and validation
//

import SwiftUI

/// Enhanced settings screen with notification preferences and device information
struct EnhancedSettingsView: View {
    // Reference to BLE manager for device communication
    @ObservedObject var bleManager: BLEManager
    
    
    // MARK: - Configuration State
    @State private var notificationsEnabled: Bool
    @State private var connectionTimeout: Double
    
    // MARK: - User Stats Persistence
    @AppStorage("userHeightInches") private var userHeightInches: Double = 70.0
    @AppStorage("userWeightLbs") private var userWeightLbs: Double = 170.0
    
    // MARK: - Notification Preferences
    @AppStorage("heartRateAlertsEnabled") private var heartRateAlertsEnabled = true
    @AppStorage("stepGoalAlertsEnabled") private var stepGoalAlertsEnabled = true
    @AppStorage("lowBatteryAlertsEnabled") private var lowBatteryAlertsEnabled = true
    @AppStorage("heartRateThreshold") private var heartRateThreshold = 100
    @AppStorage("stepGoal") private var stepGoal = 10000
    @AppStorage("batteryThreshold") private var batteryThreshold = 20
    
    // MARK: - UI State
    @State private var showSaveAlert = false
    @State private var showValidationError = false
    @State private var validationMessage = ""
    
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
                // MARK: - Device Information Section
                Section(header: Text("Device Information")) {
                    // Device name
                    HStack {
                        Label("Device", systemImage: "antenna.radiowaves.left.and.right")
                        Spacer()
                        Text(bleManager.deviceName)
                            .foregroundColor(.secondary)
                    }
                    
                    // Connection status
                    HStack {
                        Label("Status", systemImage: bleManager.isConnected ? "checkmark.circle.fill" : "xmark.circle.fill")
                            .foregroundColor(bleManager.isConnected ? .green : .red)
                        Spacer()
                        Text(bleManager.isConnected ? "Connected" : "Disconnected")
                            .foregroundColor(.secondary)
                    }
                    
                    // Signal quality
                    if bleManager.isConnected {
                        HStack {
                            Label("Signal", systemImage: "dot.radiowaves.left.and.right")
                            Spacer()
                            SignalStrengthView(rssi: bleManager.rssi)
                            Text("\(bleManager.rssi) dBm")
                                .foregroundColor(.secondary)
                                .font(.caption)
                        }
                    }
                    
                    // Firmware version (placeholder - would come from device)
                    HStack {
                        Label("Firmware", systemImage: "memorychip")
                        Spacer()
                        Text("v1.2.3")
                            .foregroundColor(.secondary)
                    }
                    
                    // Battery health (calculated from current level)
                    if let latestBattery = getLatestBatteryLevel() {
                        HStack {
                            Label("Battery Health", systemImage: "battery.100")
                            Spacer()
                            Text(batteryHealthStatus(level: latestBattery))
                                .foregroundColor(batteryHealthColor(level: latestBattery))
                        }
                    }
                }
                
                // MARK: - Connection Management Section
                Section(header: Text("Connection")) {
                    // Scan button
                    Button(action: {
                        if bleManager.isScanning {
                            bleManager.stopScanning()
                        } else {
                            bleManager.startScanning()
                        }
                    }) {
                        Label(
                            bleManager.isScanning ? "Stop Scanning" : "Scan for Device",
                            systemImage: bleManager.isScanning ? "stop.circle" : "magnifyingglass"
                        )
                    }
                    .disabled(bleManager.isConnected)
                    
                    // Connect/Disconnect button
                    Button(action: {
                        if bleManager.isConnected {
                            bleManager.disconnect()
                        } else {
                            bleManager.connect()
                        }
                    }) {
                        Label(
                            bleManager.isConnected ? "Disconnect" : "Connect",
                            systemImage: bleManager.isConnected ? "antenna.radiowaves.left.and.right.slash" : "antenna.radiowaves.left.and.right"
                        )
                        .foregroundColor(bleManager.isConnected ? .red : .blue)
                    }
                    .disabled(bleManager.discoveredPeripheral == nil && !bleManager.isConnected)
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
                Section(
                    header: Text("Device Configuration"),
                    footer: Text("Adjust device settings. Changes will be written to the device when you tap Save Configuration.")
                ) {
                    // Connection timeout stepper
                    VStack(alignment: .leading, spacing: 8) {
                        Stepper(value: $connectionTimeout, in: 5...60, step: 5) {
                            HStack {
                                Label("Connection Timeout", systemImage: "clock")
                                Spacer()
                                Text("\(Int(connectionTimeout))s")
                                    .foregroundColor(.secondary)
                                    .fontWeight(.semibold)
                            }
                        }
                        
                        Text("Time to wait before timing out connection attempts.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    .padding(.vertical, 4)
                    
                    // BLE notifications toggle
                    Toggle(isOn: $notificationsEnabled) {
                        VStack(alignment: .leading) {
                            Label("BLE Notifications", systemImage: "bell.badge")
                            Text("Enable real-time data notifications")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                    }
                    
                    // Save configuration button
                    Button(action: saveConfiguration) {
                        Label("Save Configuration", systemImage: "square.and.arrow.down")
                            .frame(maxWidth: .infinity)
                    }
                    .disabled(!bleManager.isConnected)
                }
                
                // MARK: - Notification Preferences Section
                Section(
                    header: Text("Alert Preferences"),
                    footer: Text("Configure when you want to receive notifications about your health data.")
                ) {
                    // Heart rate alerts
                    Toggle(isOn: $heartRateAlertsEnabled) {
                        VStack(alignment: .leading) {
                            Label("Heart Rate Alerts", systemImage: "heart.fill")
                                .foregroundColor(.red)
                            Text("Alert when heart rate exceeds threshold")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                    }
                    
                    // Heart rate threshold (only shown if alerts enabled)
                    if heartRateAlertsEnabled {
                        Stepper(value: $heartRateThreshold, in: 60...180, step: 5) {
                            HStack {
                                Text("   Threshold")
                                Spacer()
                                Text("\(heartRateThreshold) bpm")
                                    .foregroundColor(.secondary)
                            }
                        }
                    }
                    
                    // Step goal alerts
                    Toggle(isOn: $stepGoalAlertsEnabled) {
                        VStack(alignment: .leading) {
                            Label("Step Goal Alerts", systemImage: "figure.walk")
                                .foregroundColor(.blue)
                            Text("Alert when you reach your daily step goal")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                    }
                    
                    // Step goal (only shown if alerts enabled)
                    if stepGoalAlertsEnabled {
                        Stepper(value: $stepGoal, in: 1000...20000, step: 1000) {
                            HStack {
                                Text("   Goal")
                                Spacer()
                                Text("\(stepGoal) steps")
                                    .foregroundColor(.secondary)
                            }
                        }
                    }
                    
                    // Low battery alerts
                    Toggle(isOn: $lowBatteryAlertsEnabled) {
                        VStack(alignment: .leading) {
                            Label("Low Battery Alerts", systemImage: "battery.25")
                                .foregroundColor(.orange)
                            Text("Alert when device battery is low")
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                    }
                    
                    // Battery threshold (only shown if alerts enabled)
                    if lowBatteryAlertsEnabled {
                        Stepper(value: $batteryThreshold, in: 5...50, step: 5) {
                            HStack {
                                Text("   Threshold")
                                Spacer()
                                Text("\(batteryThreshold)%")
                                    .foregroundColor(.secondary)
                            }
                        }
                    }
                }
                
                // MARK: - Data Quality Section
                Section(header: Text("Data Quality")) {
                    HStack {
                        Label("Current Quality", systemImage: "checkmark.seal")
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
                        Label("Read from Device", systemImage: "arrow.down.circle")
                    }
                    .disabled(!bleManager.isConnected)
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)

            .alert("Configuration Saved", isPresented: $showSaveAlert) {
                Button("OK", role: .cancel) { }
            } message: {
                Text("Your configuration has been written to the device.")
            }
            .alert("Validation Error", isPresented: $showValidationError) {
                Button("OK", role: .cancel) { }
            } message: {
                Text(validationMessage)
            }
        }
    }
    
    // MARK: - Helper Functions
    
    /// Validates and saves configuration to device
    private func saveConfiguration() {
        var config = DeviceConfiguration()
        config.notificationsEnabled = notificationsEnabled
        config.connectionTimeout = Int(connectionTimeout)
        
        // Validate configuration
        if config.isValid {
            bleManager.writeConfiguration(config)
            showSaveAlert = true
        } else {
            validationMessage = "Invalid configuration values. Please check your settings."
            showValidationError = true
        }
    }
    
    /// Get latest battery level from BLE manager (would need to be tracked)
    private func getLatestBatteryLevel() -> Int16? {
        // This would ideally come from the latest reading
        // For now, return a placeholder
        return 85
    }
    
    /// Returns battery health status string
    private func batteryHealthStatus(level: Int16) -> String {
        switch level {
        case 80...100: return "Excellent"
        case 50..<80: return "Good"
        case 20..<50: return "Fair"
        default: return "Poor"
        }
    }
    
    /// Returns color for battery health
    private func batteryHealthColor(level: Int16) -> Color {
        switch level {
        case 80...100: return .green
        case 50..<80: return .blue
        case 20..<50: return .orange
        default: return .red
        }
    }
}

// MARK: - Preview

struct EnhancedSettingsView_Previews: PreviewProvider {
    static var previews: some View {
        EnhancedSettingsView(bleManager: BLEManager())
    }
}
