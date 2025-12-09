//
// BLEManager.swift
// PatekDigitale
//
// Created by vishalm3416 on 10/13/25.
//

import CoreBluetooth
import CoreData
import SwiftUI

// Structure for storing user data
struct HealthReadingData: Codable {
    let timestamp: Date
    let heartRate: Int
    let stepCount: Int
    let batteryLevel: Int
    let temperature: Double
}

class BLEManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    
    // MARK: - Core Properties
    
    // Health repository for database operations
    private var healthRepository: HealthRepository?
    
    // Supabase repository for fetching remote data
    private var supabaseRepository = SupabaseHealthRepository()

    // Bluetooth instance & peripheral variables
    var centralManager: CBCentralManager!
    var discoveredPeripheral: CBPeripheral?
    
    // CoreData context for saving readings
    var context: NSManagedObjectContext?
    
    // MARK: - Published UI State
    
    // Connection status
    @Published var isConnected = false
    @Published var isScanning = false
    
    // Signal strength indicator (-100 to 0 dBm)
    @Published var rssi: Int = -100
    
    // Device information
    @Published var deviceName: String = "Unknown Device"
    
    // Current device configuration
    @Published var configuration = DeviceConfiguration()
    
    // Latest quality flags for UI display
    @Published var latestQualityFlags = QualityFlags(rawValue: 0)
    
    // MARK: - BLE UUIDs
    
    // Service UUID for health data
    let espServiceUUID = CBUUID(string: "12345678-1234-5678-1234-56789abc0000")
    
    // Characteristic UUIDs
    let dataStreamUUID = CBUUID(string: "12345678-1234-5678-1234-56789abc1001")  // Data stream (Notify)
    let controlUUID = CBUUID(string: "12345678-1234-5678-1234-56789abc1002")     // Control (Write)
    
    // MARK: - State Restoration Keys
    
    // Keys for saving/restoring BLE state when app is terminated
    private let peripheralIdentifierKey = "peripheralIdentifier"
    private let lastConnectionKey = "lastConnection"
    
    // MARK: - Performance Monitoring
    
    // Track latency for <10ms requirement
    private var lastNotificationTime: Date?
    
    // Buffer for Supabase upload
    private var sessionReadings: [FirmwareRecord] = []
    
    // MARK: - Initialization
    
    override init() {
        super.init()
        
        // Initialize CoreBluetooth with state restoration
        centralManager = CBCentralManager(
            delegate: self,
            queue: nil,
            options: [CBCentralManagerOptionRestoreIdentifierKey: "com.patekdigitale.blemanager"]
        )
    }
    
    // Sets CoreData context for saving readings
    func setContext(_ context: NSManagedObjectContext) {
        self.context = context
        self.healthRepository = HealthRepository(context: context)
    }
    
    // MARK: - Connection Management
    
    // Manually start scanning for ESP32 device
    func startScanning() {
        guard centralManager.state == .poweredOn else {
            print("Bluetooth not powered on")
            return
        }
        
        isScanning = true
        
        centralManager.scanForPeripherals(
            withServices: [espServiceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: true]
        )
        
        print("Started scanning for ESP32 device...")
    }
    
    // Manually request data transfer from device
    func requestDataTransfer() {
        guard let peripheral = discoveredPeripheral, isConnected else {
            print("Cannot request transfer: Device not connected")
            return
        }
        
        guard let service = peripheral.services?.first(where: { $0.uuid == espServiceUUID }),
              let characteristic = service.characteristics?.first(where: { $0.uuid == controlUUID }) else {
            print("Cannot request transfer: Control characteristic not found")
            return
        }
        
        let sendCommand = "SEND"
        if let data = sendCommand.data(using: .ascii) {
            peripheral.writeValue(data, for: characteristic, type: .withoutResponse)
            print("Manually sent SEND command")
        }
    }
    
    // Manually stop scanning
    func stopScanning() {
        centralManager.stopScan()
        isScanning = false
        print("Stopped scanning")
    }
    
    // Manually connect to discovered device
    func connect() {
        guard let peripheral = discoveredPeripheral else {
            print("No peripheral to connect to")
            return
        }
        
        // Attempt connection with timeout
        centralManager.connect(peripheral, options: [
            CBConnectPeripheralOptionNotifyOnConnectionKey: true,
            CBConnectPeripheralOptionNotifyOnDisconnectionKey: true
        ])
        
        print("Connecting to \(peripheral.name ?? "Unknown")...")
    }
    
    // Manually disconnect from device
    func disconnect() {
        guard let peripheral = discoveredPeripheral else { return }
        
        centralManager.cancelPeripheralConnection(peripheral)
        isConnected = false
        print("Disconnected from device")
    }
    
    // Save peripheral identifier for state restoration
    private func savePeripheralIdentifier(_ identifier: UUID) {
        UserDefaults.standard.set(identifier.uuidString, forKey: peripheralIdentifierKey)
        UserDefaults.standard.set(Date(), forKey: lastConnectionKey)
    }
    
    // Restore previous connection if available
    private func restoreConnection() {
        guard let uuidString = UserDefaults.standard.string(forKey: peripheralIdentifierKey),
              let uuid = UUID(uuidString: uuidString) else {
            print("No previous connection to restore")
            return
        }
        
        // Check if last connection was recent (within 24 hours)
        if let lastConnection = UserDefaults.standard.object(forKey: lastConnectionKey) as? Date,
           Date().timeIntervalSince(lastConnection) < 86400 {
            
            let peripherals = centralManager.retrievePeripherals(withIdentifiers: [uuid])
            if let peripheral = peripherals.first {
                discoveredPeripheral = peripheral
                peripheral.delegate = self
                centralManager.connect(peripheral, options: nil)
                print("Attempting to restore connection to \(peripheral.name ?? "device")")
            }
        }
    }
    
    // MARK: - Device Configuration
    
    // Configuration is not currently supported by firmware
    func writeConfiguration(_ config: DeviceConfiguration) {
        print("Configuration write not supported by current firmware")
    }
    
    func readConfiguration() {
        print("Configuration read not supported by current firmware")
    }
    
    // MARK: - RSSI Monitoring
    
    // Request signal strength update
    func updateRSSI() {
        discoveredPeripheral?.readRSSI()
    }
    
    // Start periodic RSSI updates (every 2 seconds)
    private func startRSSIUpdates() {
        Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            guard let self = self, self.isConnected else { return }
            self.updateRSSI()
        }
    }
    
    // MARK: - CBCentralManagerDelegate
    
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            print("Bluetooth is powered on")
            
            restoreConnection()
            
        case .poweredOff:
            print("Bluetooth is powered off")
            isConnected = false
            
        case .unauthorized:
            print("Bluetooth unauthorized")
            
        case .unsupported:
            print("Bluetooth not supported on this device")
            
        default:
            print("Bluetooth state: \(central.state.rawValue)")
        }
    }
    
    // Called when state restoration occurs (app was terminated and restarted by iOS)
    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String : Any]) {
        if let peripherals = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral] {
            for peripheral in peripherals {
                discoveredPeripheral = peripheral
                peripheral.delegate = self
                
                if peripheral.state == .connected {
                    DispatchQueue.main.async {
                        self.isConnected = true
                        self.deviceName = peripheral.name ?? "ESP32 Device"
                    }
                    peripheral.discoverServices([espServiceUUID])
                    print("Restored connection to \(peripheral.name ?? "device")")
                } else {
                    central.connect(peripheral, options: nil)
                }
            }
        }
        
        if let _ = dict[CBCentralManagerRestoredStateScanServicesKey] {
            DispatchQueue.main.async {
                self.isScanning = true
            }
        }
    }
    
    // Called when a peripheral is discovered during scanning
    func centralManager(_ central: CBCentralManager,
                       didDiscover peripheral: CBPeripheral,
                       advertisementData: [String : Any],
                       rssi RSSI: NSNumber) {
        
        DispatchQueue.main.async {
            self.rssi = RSSI.intValue
        }
        
        if discoveredPeripheral == nil {
            discoveredPeripheral = peripheral
            peripheral.delegate = self
            
            DispatchQueue.main.async {
                self.deviceName = peripheral.name ?? "ESP32 Device"
            }
            
            stopScanning()
            connect()
        }
        
        print("Discovered \(peripheral.name ?? "Unknown") with RSSI: \(RSSI)")
    }
    
    // Called when connection succeeds
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        print("Connected to \(peripheral.name ?? "device")")
        
        DispatchQueue.main.async {
            self.isConnected = true
            self.deviceName = peripheral.name ?? "ESP32 Device"
        }
        
        savePeripheralIdentifier(peripheral.identifier)
        
        peripheral.discoverServices([espServiceUUID])
        
        startRSSIUpdates()
    }
    
    // Called when connection fails
    func centralManager(_ central: CBCentralManager,
                       didFailToConnect peripheral: CBPeripheral,
                       error: Error?) {
        print("Failed to connect: \(error?.localizedDescription ?? "unknown error")")
        
        DispatchQueue.main.async {
            self.isConnected = false
        }
        
        DispatchQueue.global().asyncAfter(deadline: .now() + 5.0) {
            self.connect()
        }
    }
    
    // Called when disconnected
    func centralManager(_ central: CBCentralManager,
                       didDisconnectPeripheral peripheral: CBPeripheral,
                       error: Error?) {
        print("Disconnected: \(error?.localizedDescription ?? "manual disconnect")")
        
        DispatchQueue.main.async {
            self.isConnected = false
        }
        
        if error != nil {
            DispatchQueue.global().asyncAfter(deadline: .now() + 3.0) {
                self.connect()
            }
        }
    }
    
    // MARK: - CBPeripheralDelegate
    
    // Called when services are discovered
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil else {
            print("Error discovering services: \(error!.localizedDescription)")
            return
        }
        
        for service in peripheral.services ?? [] {
            if service.uuid == espServiceUUID {
                peripheral.discoverCharacteristics([
                    dataStreamUUID,
                    controlUUID
                ], for: service)
            }
        }
    }
    
    // Called when characteristics are discovered
    func peripheral(_ peripheral: CBPeripheral,
                   didDiscoverCharacteristicsFor service: CBService,
                   error: Error?) {
        guard error == nil else {
            print("Error discovering characteristics: \(error!.localizedDescription)")
            return
        }
        
        for characteristic in service.characteristics ?? [] {
            if characteristic.uuid == dataStreamUUID {
                peripheral.setNotifyValue(true, for: characteristic)
                print("Subscribed to data stream notifications")
            }
            
            if characteristic.uuid == controlUUID {
                print("Found control characteristic")
                
                // Send Time Sync
                let timeCommand = "TIME:\(Int(Date().timeIntervalSince1970))"
                if let data = timeCommand.data(using: .ascii) {
                    peripheral.writeValue(data, for: characteristic, type: .withResponse)
                    print("Sent time sync: \(timeCommand)")
                }
                
                // Request Data Stream
                let sendCommand = "SEND"
                if let data = sendCommand.data(using: .ascii) {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                        peripheral.writeValue(data, for: characteristic, type: .withoutResponse)
                        print("Sent SEND command")
                    }
                }
            }
        }
    }
    
    // Called when characteristic value updates (notifications or reads)
    func peripheral(_ peripheral: CBPeripheral,
                   didUpdateValueFor characteristic: CBCharacteristic,
                   error: Error?) {
        guard error == nil, let data = characteristic.value else {
            print("Error reading characteristic: \(error?.localizedDescription ?? "no data")")
            return
        }
        
        switch characteristic.uuid {
        case dataStreamUUID:
            handleDataStream(data)
        default:
            break
        }
    }
    
    // Handle binary stream from firmware
    private func handleDataStream(_ data: Data) {
        guard !data.isEmpty else { return }
        
        let marker = data[0]
        
        switch marker {
        case BLEProtocolParser.START_MARKER:
            // Payload: 4 bytes count
            if data.count >= 5 {
                let count = data.subdata(in: 1..<5).withUnsafeBytes { $0.load(as: UInt32.self) }
                print("Start marker received. Expecting \(count) records.")
                sessionReadings.removeAll() // Clear buffer for new session
            }
            
        case BLEProtocolParser.DATA_MARKER:
            // Payload: Record struct
            // Skip the marker byte (index 0)
            if data.count > 1, let record = BLEProtocolParser.parseRecord(data.subdata(in: 1..<data.count)) {
                saveRecord(record)
                sessionReadings.append(record) // Add to buffer
            }
            
        case BLEProtocolParser.END_MARKER:
            print("End marker received. Transfer complete.")
            uploadSessionToSupabase() // Trigger upload
            
        default:
            break
        }
    }
    
    private func saveRecord(_ record: FirmwareRecord) {
        guard let repository = healthRepository else { return }
        
        repository.saveBatchReading(
            timestamp: record.timestamp,
            heartRate: Int16(record.heartRate),
            stepCount: Int32(record.stepCount),
            temperature: record.temperature,
            batteryLevel: 100, // Not sent by firmware currently
            qualityFlags: QualityFlags(rawValue: 0)
        )
    }
    
    // Called when write completes
    func peripheral(_ peripheral: CBPeripheral,
                   didWriteValueFor characteristic: CBCharacteristic,
                   error: Error?) {
        if let error = error {
            print("Error writing to characteristic: \(error.localizedDescription)")
        } else {
            print("Successfully wrote to \(characteristic.uuid)")
        }
    }
    
    // Called when RSSI is read
    func peripheral(_ peripheral: CBPeripheral, didReadRSSI RSSI: NSNumber, error: Error?) {
        guard error == nil else { return }
        
        DispatchQueue.main.async {
            self.rssi = RSSI.intValue
        }
    }
    
    // MARK - Supabase Data Fetching

    private func uploadSessionToSupabase() {
        guard !sessionReadings.isEmpty else { return }
        
        let recordsToUpload = sessionReadings
        sessionReadings.removeAll()
        
        print("☁️ Uploading \(recordsToUpload.count) 15-second consolidated records to Supabase...")
        
        Task {
            do {
                // 1. Upload Readings
                let insertRows = recordsToUpload.map { record in
                    HealthReadingInsert(
                        device_id: "esp32-devkit",
                        heart_rate: record.heartRate,
                        temperature: record.temperature,
                        steps: record.stepCount,
                        timestamp: record.timestamp,
                        epoch_min: Int(record.timestamp.timeIntervalSince1970) / 60
                    )
                }
                
                try await supabaseRepository.uploadReadings(insertRows)
                
                // 2. Calculate and Upload Summary
                let totalSteps = recordsToUpload.reduce(0) { $0 + $1.stepCount }
                
                // Retrieve user stats from UserDefaults (set in SettingsView)
                let userHeight = UserDefaults.standard.double(forKey: "userHeightInches")
                let userWeight = UserDefaults.standard.double(forKey: "userWeightLbs")
                
                // Defaults if not set: 5'10" (70 in) and 170 lbs
                let heightInches = userHeight > 0 ? userHeight : 70.0
                let weightLbs = userWeight > 0 ? userWeight : 170.0
                
                // Calculate Distance
                let heightFeet = heightInches / 12.0
                let strideLengthFeet = heightFeet * 0.43
                let rawDistance = (strideLengthFeet * Double(totalSteps)) / 5280.0
                
                // Calculate Calories
                let caloriesPerMile = weightLbs * 0.57
                let rawCalories = rawDistance * caloriesPerMile
                
                // Format for storage (Distance to 0.01, Calories to whole number)
                let totalDistance = (rawDistance * 100).rounded() / 100.0
                let totalCalories = rawCalories.rounded()
                
                let timestamps = recordsToUpload.map { $0.timestamp }
                let start = timestamps.min() ?? Date()
                let end = timestamps.max() ?? Date()
                
                let summary = HealthSummaryInsert(
                    device_id: "esp32-devkit",
                    total_steps: totalSteps,
                    total_distance_miles: totalDistance,
                    total_calories: totalCalories,
                    record_count: recordsToUpload.count,
                    start_timestamp: start,
                    end_timestamp: end,
                    upload_timestamp: Date(),
                    height_inches: heightInches,
                    weight_lbs: weightLbs
                )
                
                try await supabaseRepository.uploadSummary(summary)
                
                // 3. Erase Device Storage
                await sendEraseCommand()
                
            } catch {
                print("❌ Supabase upload failed: \(error)")
            }
        }
    }
    
    private func sendEraseCommand() async {
        guard let peripheral = discoveredPeripheral,
              let service = peripheral.services?.first(where: { $0.uuid == espServiceUUID }),
              let controlChar = service.characteristics?.first(where: { $0.uuid == controlUUID }) else {
            print("Cannot erase: Control characteristic not found")
            return
        }
        
        let eraseCommand = "ERASE"
        if let data = eraseCommand.data(using: .ascii) {
            peripheral.writeValue(data, for: controlChar, type: .withResponse)
            print("🧹 Sent ERASE command to device")
        }
    }

    func fetchSupabaseData() async {
        do {
            print("🔍 Starting Supabase fetch...")
            let readings = try await supabaseRepository.fetchRecentReadings(limit: 100)
            print("📦 Raw response count: \(readings.count)")
            
            await MainActor.run {
                for reading in readings {
                    guard let repository = healthRepository,
                          let timestamp = reading.timestamp else { return }
                    
                    // Check if this timestamp already exists
                    let fetchRequest = NSFetchRequest<HealthReading>(entityName: "HealthReading")
                    fetchRequest.predicate = NSPredicate(format: "timestamp == %@", timestamp as NSDate)
                    
                    if let count = try? context?.fetch(fetchRequest).count, count == 0 {
                        // Only save if it doesn't already exist
                        repository.saveBatchReading(
                            timestamp: timestamp,
                            heartRate: Int16(reading.heart_rate ?? 0),
                            stepCount: Int32(reading.steps ?? 0),
                            temperature: (reading.temperature ?? 0.0) * 9/5 + 32,  // Convert C to F
                            batteryLevel: 100,
                            qualityFlags: QualityFlags(rawValue: 0)
                        )
                    }
                }
                print("✅ Processed \(readings.count) readings from Supabase")
            }
        } catch {
            print("❌ Error: \(error)")
        }
    }



}
