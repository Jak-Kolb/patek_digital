//
//  ChartsView.swift
//  PatekDigitale
//
//  Data visualization with Swift Charts
//

import SwiftUI
import Charts

/// Main view that displays health data charts with time range filtering
struct ChartsView: View {
    // Currently selected time range for data filtering
    @State private var selectedTimeRange: TimeRange = .today
    
    // Selected detail reading when user taps a chart point
    @State private var selectedReading: HealthReadingRow?
    
    // Show detail overlay when a data point is tapped
    @State private var showDetail = false
    
    // Supabase Repository
    private let supabase = SupabaseHealthRepository()
    
    // Local State for Chart Data
    @State private var readings: [HealthReadingRow] = []
    @State private var isLoading: Bool = false
    
    var body: some View {
        NavigationView {
            ScrollView {
                VStack(spacing: 20) {
                    // MARK: - Time Range Picker
                    // Segmented control to switch between Today, Week, and Month views
                    Picker("Time Range", selection: $selectedTimeRange) {
                        ForEach(TimeRange.allCases, id: \.self) { range in
                            Text(range.rawValue).tag(range)
                        }
                    }
                    .pickerStyle(.segmented)
                    .padding(.horizontal)
                    .onChange(of: selectedTimeRange) { _ in
                        Task {
                            await fetchChartData()
                        }
                    }
                    
                    if isLoading {
                        ProgressView("Loading chart data...")
                            .padding()
                            .frame(height: 200)
                    } else {
                        // MARK: - Statistics Summary Card
                        // Display aggregate statistics for the selected time range
                        StatisticsCardView(readings: readings)
                            .padding(.horizontal)
                        
                        // MARK: - Chart Sections
                        // Each chart displays a different health metric
                        
                        // Heart Rate Line Chart (bpm over time)
                        HeartRateChartView(
                            readings: readings,
                            selectedReading: $selectedReading,
                            showDetail: $showDetail
                        )
                        .frame(height: 250)
                        .padding()
                        .background(Color(.systemBackground))
                        .cornerRadius(12)
                        .shadow(radius: 2)
                        .padding(.horizontal)
                        
                        // Step Count Bar Chart (daily totals)
                        StepCountChartView(
                            readings: readings,
                            selectedReading: $selectedReading,
                            showDetail: $showDetail
                        )
                        .frame(height: 250)
                        .padding()
                        .background(Color(.systemBackground))
                        .cornerRadius(12)
                        .shadow(radius: 2)
                        .padding(.horizontal)
                        
                        // Temperature Area Chart (temperature trends)
                        TemperatureChartView(
                            readings: readings,
                            selectedReading: $selectedReading,
                            showDetail: $showDetail
                        )
                        .frame(height: 250)
                        .padding()
                        .background(Color(.systemBackground))
                        .cornerRadius(12)
                        .shadow(radius: 2)
                        .padding(.horizontal)
                    }
                }
                .padding(.vertical)
            }
            .navigationTitle("Health Charts")
            .navigationBarTitleDisplayMode(.large)
            // Detail overlay shown when user taps a chart data point
            .sheet(isPresented: $showDetail) {
                if let reading = selectedReading {
                    ReadingDetailView(reading: reading)
                }
            }
            .task {
                await fetchChartData()
            }
            .refreshable {
                await fetchChartData()
            }
        }
    }
    
    private func fetchChartData() async {
        // Only show full-screen loading if we have no data
        if readings.isEmpty {
            isLoading = true
        }
        defer { isLoading = false }
        
        do {
            let now = Date()
            let startDate = selectedTimeRange.startDate
            
            // Fetch readings from Supabase
            readings = try await supabase.fetchReadings(from: startDate, to: now)
            
        } catch {
            print("Error fetching chart data: \(error)")
            readings = []
        }
    }
}

// MARK: - Time Range Enum

/// Represents the available time ranges for filtering chart data
enum TimeRange: String, CaseIterable {
    case today = "Today"
    case week = "Week"
    case month = "Month"
    
    /// Returns the start date for this time range
    var startDate: Date {
        let calendar = Calendar.current
        let now = Date()
        
        switch self {
        case .today:
            // Start of today (midnight)
            return calendar.startOfDay(for: now)
        case .week:
            // 7 days ago
            return calendar.date(byAdding: .day, value: -7, to: now) ?? now
        case .month:
            // 30 days ago
            return calendar.date(byAdding: .day, value: -30, to: now) ?? now
        }
    }
    
    /// Returns the appropriate date format string for axis labels
    var dateFormat: String {
        switch self {
        case .today:
            return "HH:mm" // Hour:minute for today
        case .week:
            return "E" // Day of week abbreviation
        case .month:
            return "MM/dd" // Month/day
        }
    }
}

// MARK: - Statistics Card View

/// Displays aggregate statistics for the selected time range
struct StatisticsCardView: View {
    let readings: [HealthReadingRow]
    
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Summary Statistics")
                .font(.headline)
                .padding(.bottom, 4)
            
            // Display statistics in a grid layout
            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 16) {
                // Average Heart Rate
                StatItemView(
                    icon: "heart.fill",
                    color: .red,
                    title: "Avg Heart Rate",
                    value: "\(averageHeartRate) bpm"
                )
                
                // Total Steps
                StatItemView(
                    icon: "figure.walk",
                    color: .blue,
                    title: "Total Steps",
                    value: "\(totalSteps)"
                )
                
                // Temperature Range
                StatItemView(
                    icon: "thermometer",
                    color: .orange,
                    title: "Temp Range",
                    value: "\(minTemp)° - \(maxTemp)°F"
                )
                
                // Average Battery
                StatItemView(
                    icon: "battery.100",
                    color: .green,
                    title: "Avg Battery",
                    value: "100%" // Placeholder
                )
            }
        }
        .padding()
        .background(Color(.secondarySystemBackground))
        .cornerRadius(12)
    }
    
    // MARK: - Computed Statistics
    
    /// Calculate average heart rate across all readings
    private var averageHeartRate: Int {
        guard !readings.isEmpty else { return 0 }
        let sum = readings.reduce(0) { $0 + Int($1.heart_rate ?? 0) }
        return sum / readings.count
    }
    
    /// Calculate total steps across all readings
    private var totalSteps: Int {
        readings.reduce(0) { $0 + Int($1.steps ?? 0) }
    }
    
    /// Find minimum temperature
    private var minTemp: Int {
        guard let min = readings.compactMap({ $0.temperature }).min() else { return 0 }
        return Int((min * 9/5) + 32)
    }
    
    /// Find maximum temperature
    private var maxTemp: Int {
        guard let max = readings.compactMap({ $0.temperature }).max() else { return 0 }
        return Int((max * 9/5) + 32)
    }
}

// MARK: - Stat Item View

/// Individual statistic item with icon, title, and value
struct StatItemView: View {
    let icon: String
    let color: Color
    let title: String
    let value: String
    
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Image(systemName: icon)
                    .foregroundColor(color)
                Text(title)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            Text(value)
                .font(.title3)
                .fontWeight(.semibold)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
        .background(Color(.systemBackground))
        .cornerRadius(8)
    }
}

// MARK: - Heart Rate Chart View

/// Line chart displaying heart rate over time
struct HeartRateChartView: View {
    let readings: [HealthReadingRow]
    @Binding var selectedReading: HealthReadingRow?
    @Binding var showDetail: Bool
    
    // Track selected timestamp for chart interaction
    @State private var selectedTimestamp: Date?
    
    var body: some View {
        VStack(alignment: .leading) {
            // Chart title
            Text("Heart Rate")
                .font(.headline)
                .padding(.bottom, 4)
            
            if readings.isEmpty {
                // No data placeholder
                Text("No heart rate data available")
                    .foregroundColor(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                // Swift Charts LineMark chart
                Chart(readings, id: \.timestamp) { reading in
                    LineMark(
                        x: .value("Time", reading.timestamp ?? Date()),
                        y: .value("BPM", reading.heart_rate ?? 0)
                    )
                    .foregroundStyle(.red)
                    .lineStyle(StrokeStyle(lineWidth: 2))
                    
                    // Add point markers for individual readings
                    PointMark(
                        x: .value("Time", reading.timestamp ?? Date()),
                        y: .value("BPM", reading.heart_rate ?? 0)
                    )
                    .foregroundStyle(.red)
                    .symbolSize(30)
                    
                    // Highlight selected point if it matches
                    if let selectedTimestamp = selectedTimestamp,
                       calendar.isDate(reading.timestamp ?? Date(), equalTo: selectedTimestamp, toGranularity: .second) {
                        PointMark(
                            x: .value("Time", reading.timestamp ?? Date()),
                            y: .value("BPM", reading.heart_rate ?? 0)
                        )
                        .foregroundStyle(.yellow)
                        .symbolSize(80)
                    }
                }
                .chartXAxis {
                    // Configure X-axis with time labels
                    AxisMarks(values: .automatic) { value in
                        AxisGridLine()
                        AxisValueLabel(format: .dateTime.hour().minute())
                    }
                }
                .chartYAxis {
                    // Configure Y-axis with BPM labels
                    AxisMarks(position: .leading) { value in
                        AxisGridLine()
                        AxisValueLabel {
                            if let intValue = value.as(Int.self) {
                                Text("\(intValue) bpm")
                                    .font(.caption)
                            }
                        }
                    }
                }
                // Enable chart selection for timestamp (tap to select)
                .chartXSelection(value: $selectedTimestamp)
                .onChange(of: selectedTimestamp) { oldValue, newValue in
                    // When user taps chart, find the closest reading
                    if let timestamp = newValue {
                        findAndSelectClosestReading(to: timestamp)
                    }
                }


            }
        }
    }
    
    /// Calendar for date comparisons
    private var calendar: Calendar {
        Calendar.current
    }
    
    /// Find the reading closest to the selected timestamp and show detail
    private func findAndSelectClosestReading(to timestamp: Date) {
        // Find the reading with timestamp closest to the tapped point
        let closest = readings.min(by: { reading1, reading2 in
            let diff1 = abs((reading1.timestamp ?? Date()).timeIntervalSince(timestamp))
            let diff2 = abs((reading2.timestamp ?? Date()).timeIntervalSince(timestamp))
            return diff1 < diff2
        })
        
        // Set the selected reading and show detail sheet
        if let closest = closest {
            selectedReading = closest
            showDetail = true
        }
    }
}

// MARK: - Step Count Chart View

/// Bar chart displaying daily step count totals
struct StepCountChartView: View {
    let readings: [HealthReadingRow]
    @Binding var selectedReading: HealthReadingRow?
    @Binding var showDetail: Bool
    
    @State private var selectedDate: Date?
    
    var body: some View {
        VStack(alignment: .leading) {
            Text("Step Count")
                .font(.headline)
                .padding(.bottom, 4)
            
            if readings.isEmpty {
                Text("No step count data available")
                    .foregroundColor(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                // BarMark chart for step counts
                Chart(aggregatedSteps, id: \.date) { item in
                    BarMark(
                        x: .value("Date", item.date),
                        y: .value("Steps", item.steps)
                    )
                    .foregroundStyle(.blue)
                }
                .chartXAxis {
                    AxisMarks(values: .automatic) { value in
                        AxisGridLine()
                        AxisValueLabel(format: .dateTime.month().day())
                    }
                }
                .chartYAxis {
                    AxisMarks(position: .leading) { value in
                        AxisGridLine()
                        AxisValueLabel {
                            if let intValue = value.as(Int.self) {
                                Text("\(intValue)")
                                    .font(.caption)
                            }
                        }
                    }
                }
                // Enable X-axis selection for date tapping
                .chartXSelection(value: $selectedDate)
                .onChange(of: selectedDate) { oldValue, newValue in
                    if let date = newValue {
                        findAndSelectReadingForDate(date)
                    }
                }

            }
        }
    }
    
    // MARK: - Daily Step Aggregation
    
    /// Aggregate steps by day for bar chart display
    private var aggregatedSteps: [(date: Date, steps: Int)] {
        let calendar = Calendar.current
        
        // Group readings by day
        let grouped = Dictionary(grouping: readings) { reading in
            calendar.startOfDay(for: reading.timestamp ?? Date())
        }
        
        // Sum steps for each day and sort by date
        return grouped.map { (date, readings) in
            let totalSteps = readings.reduce(0) { $0 + Int($1.steps ?? 0) }
            return (date: date, steps: totalSteps)
        }.sorted { $0.date < $1.date }
    }
    
    /// Find a reading for the selected date and show detail
    private func findAndSelectReadingForDate(_ date: Date) {
        let calendar = Calendar.current
        
        // Find first reading matching the selected date
        if let reading = readings.first(where: { reading in
            calendar.isDate(reading.timestamp ?? Date(), inSameDayAs: date)
        }) {
            selectedReading = reading
            showDetail = true
        }
    }
}

// MARK: - Temperature Chart View

/// Area chart displaying temperature trends over time
struct TemperatureChartView: View {
    let readings: [HealthReadingRow]
    @Binding var selectedReading: HealthReadingRow?
    @Binding var showDetail: Bool
    
    @State private var selectedTimestamp: Date?
    
    var body: some View {
        VStack(alignment: .leading) {
            Text("Temperature")
                .font(.headline)
                .padding(.bottom, 4)
            
            if readings.isEmpty {
                Text("No temperature data available")
                    .foregroundColor(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                // AreaMark chart for temperature with gradient fill
                Chart(readings, id: \.timestamp) { reading in
                    AreaMark(
                        x: .value("Time", reading.timestamp ?? Date()),
                        y: .value("°F", (reading.temperature ?? 0) * 9/5 + 32)
                    )
                    .foregroundStyle(
                        LinearGradient(
                            colors: [.orange.opacity(0.5), .orange.opacity(0.1)],
                            startPoint: .top,
                            endPoint: .bottom
                        )
                    )
                    
                    // Add line on top of area
                    LineMark(
                        x: .value("Time", reading.timestamp ?? Date()),
                        y: .value("°F", (reading.temperature ?? 0) * 9/5 + 32)
                    )
                    .foregroundStyle(.orange)
                    .lineStyle(StrokeStyle(lineWidth: 2))
                    
                    // Highlight selected point
                    if let selectedTimestamp = selectedTimestamp,
                       calendar.isDate(reading.timestamp ?? Date(), equalTo: selectedTimestamp, toGranularity: .second) {
                        PointMark(
                            x: .value("Time", reading.timestamp ?? Date()),
                            y: .value("°F", (reading.temperature ?? 0) * 9/5 + 32)
                        )
                        .foregroundStyle(.yellow)
                        .symbolSize(80)
                    }
                }
                .chartXAxis {
                    AxisMarks(values: .automatic) { value in
                        AxisGridLine()
                        AxisValueLabel(format: .dateTime.hour().minute())
                    }
                }
                .chartYAxis {
                    AxisMarks(position: .leading) { value in
                        AxisGridLine()
                        AxisValueLabel {
                            if let doubleValue = value.as(Double.self) {
                                Text(String(format: "%.1f°F", doubleValue))
                                    .font(.caption)
                            }
                        }
                    }
                }
                // Set Y-axis range for typical body temperature
                .chartYScale(domain: 60...110)
                // Enable chart selection
                .chartXSelection(value: $selectedTimestamp)
                .onChange(of: selectedTimestamp) { oldValue, newValue in
                    if let timestamp = newValue {
                        findAndSelectClosestReading(to: timestamp)
                    }
                }

            }
        }
    }
    
    private var calendar: Calendar {
        Calendar.current
    }
    
    private func findAndSelectClosestReading(to timestamp: Date) {
        let closest = readings.min(by: { reading1, reading2 in
            let diff1 = abs((reading1.timestamp ?? Date()).timeIntervalSince(timestamp))
            let diff2 = abs((reading2.timestamp ?? Date()).timeIntervalSince(timestamp))
            return diff1 < diff2
        })
        
        if let closest = closest {
            selectedReading = closest
            showDetail = true
        }
    }
}

// MARK: - Reading Detail View

/// Detail view shown when user taps a chart data point
struct ReadingDetailView: View {
    let reading: HealthReadingRow
    @Environment(\.dismiss) private var dismiss
    
    var body: some View {
        NavigationView {
            List {
                // Timestamp section
                Section(header: Text("Timestamp")) {
                    HStack {
                        Image(systemName: "clock")
                            .foregroundColor(.blue)
                        Text(reading.timestamp?.formatted(date: .long, time: .standard) ?? "Unknown")
                    }
                }
                
                // Health metrics section
                Section(header: Text("Health Metrics")) {
                    // Heart Rate
                    HStack {
                        Image(systemName: "heart.fill")
                            .foregroundColor(.red)
                        Text("Heart Rate")
                        Spacer()
                        Text("\(Int(reading.heart_rate ?? 0)) bpm")
                            .foregroundColor(.secondary)
                    }
                    
                    // Step Count
                    HStack {
                        Image(systemName: "figure.walk")
                            .foregroundColor(.blue)
                        Text("Steps")
                        Spacer()
                        Text("\(reading.steps ?? 0)")
                            .foregroundColor(.secondary)
                    }
                    
                    // Temperature
                    HStack {
                        Image(systemName: "thermometer")
                            .foregroundColor(.orange)
                        Text("Temperature")
                        Spacer()
                        Text(String(format: "%.1f°F", (reading.temperature ?? 0) * 9/5 + 32))
                            .foregroundColor(.secondary)
                    }
                    
                    // Battery Level
                    HStack {
                        Image(systemName: "battery.100")
                            .foregroundColor(.green)
                        Text("Battery")
                        Spacer()
                        Text("100%") // Placeholder
                            .foregroundColor(.secondary)
                    }
                }
            }
            .navigationTitle("Reading Detail")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") {
                        dismiss()
                    }
                }
            }
        }
    }
}

// MARK: - Preview

struct ChartsView_Previews: PreviewProvider {
    static var previews: some View {
        ChartsView()
    }
}
