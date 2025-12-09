//
//  DashboardView.swift
//  PatekDigitale
//
//  Enhanced dashboard with card-based layout and animations
//

import SwiftUI
import CoreData

/// Main dashboard displaying real-time health metrics in card-based layout
struct DashboardView: View {
    // CoreData context for fetching readings
    @Environment(\.managedObjectContext) private var context
    
    // BLE manager for device connection and data
    @ObservedObject var bleManager: BLEManager
    
    // Supabase Repository
    private let supabase = SupabaseHealthRepository()
    
    // Local State for Dashboard Data (Fetched from Supabase)
    @State private var dailySteps: Int = 0
    @State private var dailyCalories: Double = 0.0
    @State private var latestHeartRate: Int = 0
    @State private var latestTemperature: Double = 0.0
    @State private var latestBattery: Int = 0
    @State private var lastUpdate: Date?
    @State private var isLoading: Bool = false
    
    // Animation state for heart rate pulsing
    @State private var heartPulse = false
    
    // Timer for auto-refresh
    let timer = Timer.publish(every: 30, on: .main, in: .common).autoconnect()

    var body: some View {
        NavigationView {
            ScrollView {
                VStack(spacing: 20) {
                    // MARK: - Connection Status Card
                    ConnectionStatusCard(bleManager: bleManager)
                        .padding(.horizontal)
                    
                    // MARK: - Health Metrics Cards
                    if let lastUpdate = lastUpdate {
                        // Heart Rate Card with pulsing animation
                        HealthMetricCard(
                            title: "Heart Rate",
                            value: "\(latestHeartRate)",
                            unit: "bpm",
                            icon: "heart.fill",
                            color: .red,
                            isPulsing: true
                        )
                        .padding(.horizontal)
                        
                        // Step Count Card with progress ring
                        StepCountCard(
                            steps: dailySteps,
                            goal: 10000
                        )
                        .padding(.horizontal)
                        
                        // Calories Card (New)
                        HealthMetricCard(
                            title: "Calories",
                            value: String(format: "%.0f", dailyCalories),
                            unit: "kcal",
                            icon: "flame.fill",
                            color: .orange,
                            isPulsing: false
                        )
                        .padding(.horizontal)
                        
                        // Two-column grid for Temperature and Battery
                        HStack(spacing: 16) {
                            // Temperature Card
                            CompactMetricCard(
                                title: "Temperature",
                                value: String(format: "%.1f", latestTemperature),
                                unit: "°F",
                                icon: "thermometer",
                                color: .blue
                            )
                            
                            // Battery Card
                            BatteryCard(
                                level: latestBattery,
                                icon: batteryIcon(level: Int16(latestBattery)),
                                color: batteryColor(level: Int16(latestBattery))
                            )
                        }
                        .padding(.horizontal)
                        
                        // MARK: - Last Update Timestamp
                        LastUpdateView(timestamp: lastUpdate)
                            .padding(.horizontal)
                        
                    } else {
                        // No data state
                        if isLoading {
                            ProgressView("Fetching data from cloud...")
                                .padding()
                        } else {
                            NoDataView()
                                .padding()
                        }
                    }
                }
                .padding(.vertical)
            }
            .navigationTitle("Dashboard")
            .background(Color(UIColor.systemGroupedBackground))
            .refreshable {
                await fetchSupabaseData()
            }
            .onAppear {
                Task {
                    await fetchSupabaseData()
                }
            }
            .onReceive(timer) { _ in
                Task {
                    await fetchSupabaseData()
                }
            }
        }
    }
    
    // MARK: - Data Fetching
    
    private func fetchSupabaseData() async {
        // Only show explicit loading indicator if we don't have data yet
        // (Pull-to-refresh has its own spinner)
        if lastUpdate == nil {
            isLoading = true
        }
        defer { isLoading = false }
        
        do {
            // 1. Calculate time range for "Today"
            let calendar = Calendar.current
            let startOfDay = calendar.startOfDay(for: Date())
            let endOfDay = calendar.date(byAdding: .day, value: 1, to: startOfDay)!
            
            // 2. Fetch readings from Supabase for today
            let readings = try await supabase.fetchReadings(from: startOfDay, to: endOfDay)
            
            // 3. Process Data
            if let latest = readings.last {
                // Update latest metrics
                latestHeartRate = Int(latest.heart_rate ?? 0)
                // Convert C to F if needed, assuming Supabase stores C based on previous code
                // Firmware sends C. App converts to F for display.
                // Let's check SupabaseHealthRepository... it just returns what's in DB.
                // Firmware sends C. BLEManager uploads C?
                // BLEManager.swift:
                // HealthReadingInsert(... temperature: record.temperature ...) -> record.temperature comes from FirmwareRecord
                // FirmwareRecord: temperature: Double(tempRaw) / 100.0 -> This is C.
                // So Supabase has C.
                // Dashboard expects F.
                let tempC = latest.temperature ?? 0.0
                latestTemperature = (tempC * 9/5) + 32
                
                latestBattery = 100 // Placeholder as before
                lastUpdate = latest.timestamp
                
                // Calculate Totals
                dailySteps = readings.reduce(0) { $0 + (Int($1.steps ?? 0)) }
                
                // Calculate Calories
                let userHeight = UserDefaults.standard.double(forKey: "userHeightInches")
                let userWeight = UserDefaults.standard.double(forKey: "userWeightLbs")
                let heightInches = userHeight > 0 ? userHeight : 70.0
                let weightLbs = userWeight > 0 ? userWeight : 170.0
                
                let heightFeet = heightInches / 12.0
                let strideLengthFeet = heightFeet * 0.43
                let distanceMiles = (strideLengthFeet * Double(dailySteps)) / 5280.0
                dailyCalories = distanceMiles * weightLbs * 0.57
                
            } else {
                // No data for today
                dailySteps = 0
                dailyCalories = 0
                // Keep old latest values? Or reset?
                // If we wiped Supabase, we should reset.
                latestHeartRate = 0
                latestTemperature = 0
                lastUpdate = nil
            }
            
        } catch {
            print("Error fetching dashboard data: \(error)")
        }
    }
    
    // MARK: - Helpers

    
    private func batteryIcon(level: Int16) -> String {
        switch level {
        case 0...20: return "battery.25"
        case 21...50: return "battery.50"
        case 51...80: return "battery.75"
        default: return "battery.100"
        }
    }
    
    private func batteryColor(level: Int16) -> Color {
        switch level {
        case 0...20: return .red
        case 21...50: return .orange
        default: return .green
        }
    }
}

// MARK: - Connection StatusCard

/// Card displaying connection status with prominent indicator
struct ConnectionStatusCard: View {
    @ObservedObject var bleManager: BLEManager
    
    var body: some View {
        HStack(spacing: 12) {
            // Status indicator
            Circle()
                .fill(bleManager.isConnected ? Color.green : Color.red)
                .frame(width: 12, height: 12)
            
            VStack(alignment: .leading, spacing: 4) {
                Text(bleManager.isConnected ? "Connected" : "Disconnected")
                    .font(.headline)
                
                Text(bleManager.isConnected ? "ESP32 → Supabase" : "Waiting for device...")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
            }
            
            Spacer()
            
            // Sync Button
            if bleManager.isConnected {
                Button(action: {
                    bleManager.requestDataTransfer()
                }) {
                    HStack {
                        Image(systemName: "arrow.triangle.2.circlepath")
                        Text("Sync")
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(Color.blue.opacity(0.1))
                    .cornerRadius(8)
                }
            }
        }
        .padding()
        .background(Color(.systemBackground))
        .cornerRadius(12)
        .shadow(radius: 2)
    }
}

// MARK: - Health Metric Card

/// Large card for displaying primary health metrics with optional pulsing animation
struct HealthMetricCard: View {
    let title: String
    let value: String
    let unit: String
    let icon: String
    let color: Color
    let isPulsing: Bool
    
    @State private var pulse = false
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Title and icon
            HStack {
                Image(systemName: icon)
                    .font(.title2)
                    .foregroundColor(color)
                    .scaleEffect(isPulsing && pulse ? 1.2 : 1.0)
                    .animation(
                        isPulsing ? Animation.easeInOut(duration: 0.8).repeatForever(autoreverses: true) : .default,
                        value: pulse
                    )
                
                Text(title)
                    .font(.headline)
                    .foregroundColor(.secondary)
                
                Spacer()
            }
            
            // Value display
            HStack(alignment: .firstTextBaseline, spacing: 4) {
                Text(value)
                    .font(.system(size: 48, weight: .bold, design: .rounded))
                    .foregroundColor(.primary)
                
                Text(unit)
                    .font(.title3)
                    .foregroundColor(.secondary)
            }
        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(.secondarySystemGroupedBackground))
        .cornerRadius(12)
        .shadow(color: Color.black.opacity(0.1), radius: 5, x: 0, y: 2)
        .onAppear {
            if isPulsing {
                pulse = true
            }
        }
    }
}

// MARK: - Step Count Card with Progress Ring

/// Card displaying step count with circular progress indicator
struct StepCountCard: View {
    let steps: Int
    let goal: Int
    
    /// Calculate progress percentage (0.0 to 1.0)
    private var progress: Double {
        min(Double(steps) / Double(goal), 1.0)
    }
    
    var body: some View {
        HStack(spacing: 20) {
            // Progress ring
            ZStack {
                // Background circle
                Circle()
                    .stroke(Color.blue.opacity(0.2), lineWidth: 10)
                    .frame(width: 80, height: 80)
                
                // Progress circle
                Circle()
                    .trim(from: 0, to: progress)
                    .stroke(
                        LinearGradient(
                            colors: [.blue, .cyan],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        ),
                        style: StrokeStyle(lineWidth: 10, lineCap: .round)
                    )
                    .frame(width: 80, height: 80)
                    .rotationEffect(.degrees(-90))
                    .animation(.easeInOut(duration: 1.0), value: progress)
                
                // Percentage text
                Text("\(Int(progress * 100))%")
                    .font(.caption)
                    .fontWeight(.bold)
                    .foregroundColor(.blue)
            }
            
            // Step information
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    Image(systemName: "figure.walk")
                        .font(.title2)
                        .foregroundColor(.blue)
                    
                    Text("Steps")
                        .font(.headline)
                        .foregroundColor(.secondary)
                }
                
                // Step count
                HStack(alignment: .firstTextBaseline, spacing: 4) {
                    Text("\(steps)")
                        .font(.system(size: 32, weight: .bold, design: .rounded))
                        .foregroundColor(.primary)
                    
                    Text("/ \(goal)")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                }
            }
            
            Spacer()
        }
        .padding()
        .background(Color(.secondarySystemGroupedBackground))
        .cornerRadius(12)
        .shadow(color: Color.black.opacity(0.1), radius: 5, x: 0, y: 2)
    }
}

// MARK: - Compact Metric Card

/// Smaller card for secondary metrics (used in two-column layout)
struct CompactMetricCard: View {
    let title: String
    let value: String
    let unit: String
    let icon: String
    let color: Color
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Icon and title
            HStack {
                Image(systemName: icon)
                    .foregroundColor(color)
                Text(title)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            
            // Value
            VStack(alignment: .leading, spacing: 0) {
                Text(value)
                    .font(.system(size: 28, weight: .bold, design: .rounded))
                    .foregroundColor(.primary)
                
                Text(unit)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(.secondarySystemGroupedBackground))
        .cornerRadius(12)
        .shadow(color: Color.black.opacity(0.1), radius: 5, x: 0, y: 2)
    }
}

// MARK: - Battery Card

/// Specialized card for battery level with icon
struct BatteryCard: View {
    let level: Int
    let icon: String
    let color: Color
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            // Icon and title
            HStack {
                Image(systemName: icon)
                    .foregroundColor(color)
                Text("Battery")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            
            // Value
            VStack(alignment: .leading, spacing: 0) {
                Text("\(level)")
                    .font(.system(size: 28, weight: .bold, design: .rounded))
                    .foregroundColor(.primary)
                
                Text("%")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(.secondarySystemGroupedBackground))
        .cornerRadius(12)
        .shadow(color: Color.black.opacity(0.1), radius: 5, x: 0, y: 2)
    }
}

// MARK: - Last Update View

/// Displays the timestamp of the last data update
struct LastUpdateView: View {
    let timestamp: Date?
    
    var body: some View {
        HStack {
            Image(systemName: "clock")
                .foregroundColor(.secondary)
            
            Text("Last updated: ")
                .foregroundColor(.secondary)
            
            if let timestamp = timestamp {
                Text(timeAgo(from: timestamp))
                    .foregroundColor(.secondary)
            } else {
                Text("Never")
                    .foregroundColor(.secondary)
            }
            
            Spacer()
        }
        .font(.caption)
        .padding(.horizontal, 4)
    }
    
    /// Calculate relative time string (e.g., "2 minutes ago")
    private func timeAgo(from date: Date) -> String {
        let seconds = Date().timeIntervalSince(date)
        
        if seconds < 60 {
            return "Just now"
        } else if seconds < 3600 {
            let minutes = Int(seconds / 60)
            return "\(minutes) minute\(minutes == 1 ? "" : "s") ago"
        } else if seconds < 86400 {
            let hours = Int(seconds / 3600)
            return "\(hours) hour\(hours == 1 ? "" : "s") ago"
        } else {
            return date.formatted(date: .abbreviated, time: .shortened)
        }
    }
}

// MARK: - No Data View

/// Placeholder view when no health data is available
struct NoDataView: View {
    var body: some View {
        VStack(spacing: 20) {
            Image(systemName: "chart.line.downtrend.xyaxis")
                .font(.system(size: 60))
                .foregroundColor(.secondary)
            
            Text("No Health Data")
                .font(.title2)
                .fontWeight(.semibold)
            
            Text("Connect to your device to see your health metrics.")
                .font(.subheadline)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 40)
        }
        .padding(.vertical, 60)
    }
}

// MARK: - Preview

struct DashboardView_Previews: PreviewProvider {
    static var previews: some View {
        DashboardView(bleManager: BLEManager())
            .environment(\.managedObjectContext, DataController.shared.container.viewContext)
    }
}
