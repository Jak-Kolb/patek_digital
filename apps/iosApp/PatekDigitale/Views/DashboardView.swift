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
    
    // Fetch most recent reading
    @FetchRequest(
        sortDescriptors: [NSSortDescriptor(keyPath: \HealthReading.timestamp, ascending: false)],
        animation: .default
    )
    private var readings: FetchedResults<HealthReading>
    
    // Animation state for heart rate pulsing
    @State private var heartPulse = false
    
    // Calculate total steps for today
    private var todaysTotalSteps: Int {
        let calendar = Calendar.current
        let startOfToday = calendar.startOfDay(for: Date())
        
        // Filter readings from today and sum their step counts
        let todaysReadings = readings.filter { reading in
            guard let timestamp = reading.timestamp else { return false }
            return calendar.isDate(timestamp, inSameDayAs: startOfToday)
        }
        
        return todaysReadings.reduce(0) { $0 + Int($1.stepCount) }
    }

    
    var body: some View {
        NavigationView {
            ScrollView {
                VStack(spacing: 20) {
                    // MARK: - Connection Status Card
                    ConnectionStatusCard(bleManager: bleManager)
                        .padding(.horizontal)
                    
                    // MARK: - Health Metrics Cards
                    if let latest = readings.first {
                        // Heart Rate Card with pulsing animation
                        HealthMetricCard(
                            title: "Heart Rate",
                            value: "\(latest.heartRate)",
                            unit: "bpm",
                            icon: "heart.fill",
                            color: .red,
                            isPulsing: true
                        )
                        .padding(.horizontal)
                        
                        // Step Count Card with progress ring
                        StepCountCard(
                            steps: todaysTotalSteps,
                            goal: 10000
                        )
                        .padding(.horizontal)
                        
                        // Two-column grid for Temperature and Battery
                        HStack(spacing: 16) {
                            // Temperature Card
                            CompactMetricCard(
                                title: "Temperature",
                                value: String(format: "%.1f", latest.temperature),
                                unit: "°F",
                                icon: "thermometer",
                                color: .orange
                            )
                            
                            // Battery Card
                            BatteryCard(
                                level: Int(latest.batteryLevel),
                                icon: batteryIcon(level: latest.batteryLevel),
                                color: batteryColor(level: latest.batteryLevel)
                            )
                        }
                        .padding(.horizontal)
                        
                        // MARK: - Last Update Timestamp
                        LastUpdateView(timestamp: latest.timestamp)
                            .padding(.horizontal)
                        
                    } else {
                        // No data state
                        NoDataView()
                            .padding()
                    }
                }
                .padding(.vertical)
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("Dashboard")
            .navigationBarTitleDisplayMode(.large)
      
            .onAppear {
                // Initialize BLE manager context
                bleManager.setContext(context)
                
                // Clear all old data (TEMPORARY - remove after running once)
//                let repository = HealthRepository(context: context)
//                repository.deleteAllReadings()
                
                // Start heart pulse animation
                withAnimation(Animation.easeInOut(duration: 1.0).repeatForever(autoreverses: true)) {
                    heartPulse = true
                }
            }
            
            .task {
                // Load Supabase data when view appears
                await bleManager.fetchSupabaseData()
            }
            .refreshable {
                // Pull-to-refresh functionality
                await bleManager.fetchSupabaseData()
            }
            
        }
    }
    
    // MARK: - Helper Functions
    
    /// Returns appropriate battery icon based on level
    private func batteryIcon(level: Int16) -> String {
        switch level {
        case 75...100: return "battery.100"
        case 50..<75: return "battery.75"
        case 25..<50: return "battery.50"
        case 10..<25: return "battery.25"
        default: return "battery.0"
        }
    }
    
    /// Returns color for battery level
    private func batteryColor(level: Int16) -> Color {
        switch level {
        case 50...100: return .green
        case 20..<50: return .orange
        default: return .red
        }
    }
}

// MARK: - Connection Status Card

/// Card displaying connection status with prominent indicator
struct ConnectionStatusCard: View {
    @ObservedObject var bleManager: BLEManager
    
    var body: some View {
        HStack(spacing: 12) {
            // Status indicator - always green for Supabase
            Circle()
                .fill(Color.green)
                .frame(width: 12, height: 12)
            
            VStack(alignment: .leading, spacing: 4) {
                Text("Connected")
                    .font(.headline)
                
                Text("ESP32 → Supabase")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
            }
            
            Spacer()
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
            
            Text("Connect to your device or enable Mock Data in Settings to see your health metrics.")
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
