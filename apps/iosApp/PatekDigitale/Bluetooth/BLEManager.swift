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
    
    // Bluetooth instance & peripheral variables
    var centralManager: CBCentralManager!
    var discoveredPeripheral: CBPeripheral?
    
    // Mock mode for testing without physical device
    @Published var useMockData = true
    var mockTimer: Timer?
    
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
    let espServiceUUID = CBUUID(string: "00000000-0000-0000-0000-000000000001")
    
    // Characteristic UUIDs
    let configUUID = CBUUID(string: "00000000-0000-0000-0000-000000000006")      // Config write/read
    let heartRateUUID = CBUUID(string: "00000000-0000-0000-0000-000000000002")   // HR notifications
    let stepCountUUID = CBUUID(string: "00000000-0000-0000-0000-000000000003")   // Steps notifications
    let temperatureUUID = CBUUID(string: "00000000-0000-0000-0000-000000000004") // Temp notifications
    let batteryUUID = CBUUID(string: "00000000-0000-0000-0000-000000000005")     // Battery notifications
    let dataStreamUUID = CBUUID(string: "00000000-0000-0000-0000-000000000007")  // Combined TLV stream
    
    // MARK: - State Restoration Keys
    
    // Keys for saving/restoring BLE state when app is terminated
    private let peripheralIdentifierKey = "peripheralIdentifier"
    private let lastConnectionKey = "lastConnection"
    
    // MARK: - Performance Monitoring
    
    // Track latency for <10ms requirement
    private var lastNotificationTime: Date?
    
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
    
    // Write configuration to ESP32 via GATT
    func writeConfiguration(_ config: DeviceConfiguration) {
        guard let peripheral = discoveredPeripheral,
              let configCharacteristic = findCharacteristic(configUUID) else {
            print("Cannot write configuration: not connected or characteristic not found")
            return
        }
        
        guard config.isValid else {
            print("Invalid configuration values")
            return
        }
        
        let data = config.encode()
        peripheral.writeValue(data, for: configCharacteristic, type: .withResponse)
        
        DispatchQueue.main.async {
            self.configuration = config
        }
        
        print("Writing configuration to device: \(data.map { String(format: "%02x", $0) }.joined())")
    }
    
    // Read current configuration from ESP32
    func readConfiguration() {
        guard let peripheral = discoveredPeripheral,
              let configCharacteristic = findCharacteristic(configUUID) else {
            print("Cannot read configuration: not connected")
            return
        }
        
        peripheral.readValue(for: configCharacteristic)
        print("Requesting configuration from device")
    }
    
    // Helper to find characteristic by UUID
    private func findCharacteristic(_ uuid: CBUUID) -> CBCharacteristic? {
        guard let peripheral = discoveredPeripheral else { return nil }
        
        for service in peripheral.services ?? [] {
            for characteristic in service.characteristics ?? [] {
                if characteristic.uuid == uuid {
                    return characteristic
                }
            }
        }
        return nil
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
            
            if useMockData {
                startMockDataGeneration()
            }
            
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
        
        stopMockDataGeneration()
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
                    configUUID,
                    heartRateUUID,
                    stepCountUUID,
                    temperatureUUID,
                    batteryUUID,
                    dataStreamUUID
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
            
            if characteristic.uuid == heartRateUUID ||
               characteristic.uuid == stepCountUUID ||
               characteristic.uuid == temperatureUUID ||
               characteristic.uuid == batteryUUID {
                peripheral.setNotifyValue(true, for: characteristic)
            }
            
            if characteristic.uuid == configUUID {
                peripheral.readValue(for: characteristic)
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
        
        let receivedTime = Date()
        if let lastTime = lastNotificationTime {
            let latency = receivedTime.timeIntervalSince(lastTime) * 1000 // Convert to ms
            if latency > 10.0 {
                print("WARNING: Latency exceeded 10ms: \(latency)ms")
            }
        }
        lastNotificationTime = receivedTime
        
        switch characteristic.uuid {
        case dataStreamUUID:
            handleTLVStream(data, timestamp: receivedTime)
            
        case configUUID:
            if let config = DeviceConfiguration.decode(from: data) {
                DispatchQueue.main.async {
                    self.configuration = config
                }
                print("Received configuration: \(config)")
            }
            
        case heartRateUUID:
            handleSingleValue(data, type: 0x01, timestamp: receivedTime)
            
        case stepCountUUID:
            handleSingleValue(data, type: 0x02, timestamp: receivedTime)
            
        case temperatureUUID:
            handleSingleValue(data, type: 0x03, timestamp: receivedTime)
            
        case batteryUUID:
            handleSingleValue(data, type: 0x04, timestamp: receivedTime)
            
        default:
            break
        }
        
        let processingTime = Date().timeIntervalSince(receivedTime) * 1000
        if processingTime > 10.0 {
            print("WARNING: Processing time exceeded 10ms: \(processingTime)ms")
        }
    }
    
    // Optimized handler for combined TLV stream
    private func handleTLVStream(_ data: Data, timestamp: Date) {
        let tlvReadings = TLVParser.parse(data)
        
        guard !tlvReadings.isEmpty, let repository = healthRepository else { return }
        
        for tlv in tlvReadings where tlv.type == 0x05 {
            if let qualityByte = tlv.decodedValue() as? UInt8 {
                DispatchQueue.main.async {
                    self.latestQualityFlags = QualityFlags(rawValue: qualityByte)
                }
            }
        }
        
        TLVParser.batchProcess(tlvReadings, timestamp: timestamp, repository: repository)
    }
    
    // Handler for legacy single-value notifications
    private func handleSingleValue(_ data: Data, type: UInt8, timestamp: Date) {
        let tlv = HealthReadingTLV(type: type, length: UInt8(data.count), value: data)
        
        guard let repository = healthRepository else { return }
        
        repository.saveReading(from: tlv, timestamp: timestamp)
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
    
    // MARK: - Mock Data Generation
    
    // Starts generating mock data for testing without physical device
    func startMockDataGeneration() {
        guard useMockData, mockTimer == nil else { return }
        
        print("Starting mock data generation")
        
        // Generate data every 5 seconds
        mockTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
            self?.generateMockReading()
        }
    }
    
    // Stops mock data generation
    func stopMockDataGeneration() {
        mockTimer?.invalidate()
        mockTimer = nil
        print("Stopped mock data generation")
    }
    
    // Generates realistic mock health data
    private func generateMockReading() {
        guard let repository = healthRepository else { return }
        
        let heartRate = Int16.random(in: 60...100)
        let stepCount = Int32.random(in: 0...10000)
        let temperature = Double.random(in: 97.0...99.0)
        let batteryLevel = Int16.random(in: 20...100)
        
        var flags = QualityFlags(rawValue: 0)
        if Int.random(in: 0...10) == 0 { flags.insert(.highMotion) }
        if Int.random(in: 0...20) == 0 { flags.insert(.lowSignal) }
        
        // Save mock reading
        repository.saveBatchReading(
            timestamp: Date(),
            heartRate: heartRate,
            stepCount: stepCount,
            temperature: temperature,
            batteryLevel: batteryLevel,
            qualityFlags: flags
        )
        
        DispatchQueue.main.async {
            self.latestQualityFlags = flags
        }
        
        print("Generated mock reading: HR=\(heartRate) Steps=\(stepCount) Temp=\(temperature) Battery=\(batteryLevel)%")
    }
}
