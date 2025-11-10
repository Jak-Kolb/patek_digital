//
//  ExportView.swift
//  PatekDigitale
//
//  Export health data to CSV and share
//

import SwiftUI
import CoreData

/// View for exporting health data to various formats
struct ExportView: View {
    @Environment(\.managedObjectContext) private var context
    
    // Fetch all readings for export
    @FetchRequest(
        sortDescriptors: [NSSortDescriptor(keyPath: \HealthReading.timestamp, ascending: false)],
        animation: .default
    )
    private var readings: FetchedResults<HealthReading>
    
    // State for export options
    @State private var exportFormat: ExportFormat = .csv
    @State private var showShareSheet = false
    @State private var exportedFileURL: URL?
    @State private var showAlert = false
    @State private var alertMessage = ""
    
    var body: some View {
        NavigationView {
            List {
                // MARK: - Export Options Section
                Section(header: Text("Export Options")) {
                    // Format picker
                    Picker("Format", selection: $exportFormat) {
                        ForEach(ExportFormat.allCases, id: \.self) { format in
                            Text(format.rawValue).tag(format)
                        }
                    }
                    .pickerStyle(.segmented)
                }
                
                // MARK: - Data Summary Section
                Section(header: Text("Data Summary")) {
                    HStack {
                        Label("Total Readings", systemImage: "doc.text")
                        Spacer()
                        Text("\(readings.count)")
                            .foregroundColor(.secondary)
                    }
                    
                    if let oldest = readings.last?.timestamp,
                       let newest = readings.first?.timestamp {
                        HStack {
                            Label("Date Range", systemImage: "calendar")
                            Spacer()
                            VStack(alignment: .trailing) {
                                Text(oldest.formatted(date: .abbreviated, time: .omitted))
                                    .foregroundColor(.secondary)
                                Text("to")
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                                Text(newest.formatted(date: .abbreviated, time: .omitted))
                                    .foregroundColor(.secondary)
                            }
                        }
                    }
                }
                
                // MARK: - Export Actions Section
                Section {
                    // Export button
                    Button(action: exportData) {
                        Label("Export Data", systemImage: "square.and.arrow.up")
                            .frame(maxWidth: .infinity)
                    }
                    .disabled(readings.isEmpty)
                }
                
                // MARK: - Information Section
                Section(header: Text("Information")) {
                    Text("Export your health data to share with healthcare providers or for personal records. Exported files include timestamps, heart rate, step count, temperature, battery level, and quality flags.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .navigationTitle("Export Data")
            .navigationBarTitleDisplayMode(.large)
            .sheet(isPresented: $showShareSheet) {
                if let url = exportedFileURL {
                    ShareSheet(items: [url])
                }
            }
            .alert("Export Status", isPresented: $showAlert) {
                Button("OK", role: .cancel) { }
            } message: {
                Text(alertMessage)
            }
        }
    }
    
    // MARK: - Export Function
    
    /// Export data to selected format
    private func exportData() {
        switch exportFormat {
        case .csv:
            exportToCSV()
        case .json:
            exportToJSON()
        }
    }
    
    /// Export readings to CSV format
    private func exportToCSV() {
        var csvText = "Timestamp,Heart Rate (bpm),Step Count,Temperature (°F),Battery Level (%),Quality Flags\n"
        
        for reading in readings.reversed() {
            let timestamp = reading.timestamp?.ISO8601Format() ?? ""
            let row = "\(timestamp),\(reading.heartRate),\(reading.stepCount),\(reading.temperature),\(reading.batteryLevel),\"\(reading.qualityFlags ?? "Good")\"\n"
            csvText.append(row)
        }
        
        saveAndShare(data: csvText, filename: "health_data.csv")
    }
    
    /// Export readings to JSON format
    private func exportToJSON() {
        let exportData = readings.map { reading in
            return [
                "timestamp": reading.timestamp?.ISO8601Format() ?? "",
                "heartRate": reading.heartRate,
                "stepCount": reading.stepCount,
                "temperature": reading.temperature,
                "batteryLevel": reading.batteryLevel,
                "qualityFlags": reading.qualityFlags ?? "Good"
            ] as [String : Any]
        }
        
        if let jsonData = try? JSONSerialization.data(withJSONObject: exportData, options: .prettyPrinted),
           let jsonString = String(data: jsonData, encoding: .utf8) {
            saveAndShare(data: jsonString, filename: "health_data.json")
        }
    }
    
    /// Save data to file and present share sheet
    private func saveAndShare(data: String, filename: String) {
        let tempDir = FileManager.default.temporaryDirectory
        let fileURL = tempDir.appendingPathComponent(filename)
        
        do {
            try data.write(to: fileURL, atomically: true, encoding: .utf8)
            exportedFileURL = fileURL
            showShareSheet = true
            alertMessage = "Export successful! \(readings.count) readings exported."
        } catch {
            alertMessage = "Export failed: \(error.localizedDescription)"
            showAlert = true
        }
    }
}

// MARK: - Export Format Enum

/// Available export formats
enum ExportFormat: String, CaseIterable {
    case csv = "CSV"
    case json = "JSON"
}

// MARK: - Share Sheet

/// UIKit Share Sheet wrapper for SwiftUI
struct ShareSheet: UIViewControllerRepresentable {
    let items: [Any]
    
    func makeUIViewController(context: Context) -> UIActivityViewController {
        let controller = UIActivityViewController(activityItems: items, applicationActivities: nil)
        return controller
    }
    
    func updateUIViewController(_ uiViewController: UIActivityViewController, context: Context) {}
}

// MARK: - Preview

struct ExportView_Previews: PreviewProvider {
    static var previews: some View {
        ExportView()
            .environment(\.managedObjectContext, DataController.shared.container.viewContext)
    }
}
