//
//  BLEManager.swift
//  PatekDigitale
//
//  Created by vishalm3416 on 10/13/25.
//

import CoreBluetooth
import CoreData

// Structure for storing user data
struct HealthReadingData: Codable {
    let timestamp: Date
    let heartRate: Int
    let stepCount: Int
    let batteryLevel: Int
    let temperature: Double
}

class BLEManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    // Health repo
    private var healthRepository: HealthRepository?
    
    // Instance & peripheral variables
    var centralManager: CBCentralManager!
    var discoveredPeripheral: CBPeripheral?
    
    // Mock mode
    @Published var useMockData = true
    var mockTimer: Timer?
    
    // CoreData context
    var context: NSManagedObjectContext?
    
    // UUIDs (replace with actual)
    let espServiceUUID = CBUUID(string: "00000000-0000-0000-0000-000000000001")
    let configUUID = CBUUID(string: "00000000-0000-0000-0000-000000000006")
    
    let heartRateUUID = CBUUID(string: "00000000-0000-0000-0000-000000000002")
    let stepCountUUID = CBUUID(string: "00000000-0000-0000-0000-000000000003")
    let batteryUUID = CBUUID(string: "00000000-0000-0000-0000-000000000004")
    let temperatureUUID = CBUUID(string: "00000000-0000-0000-0000-000000000005")

    // Storage key
    let storageKey = "HealthReadings"
    
    // Observable variables
    @Published var isConnected = false;
    @Published var connectionError: String? = nil
    
    @Published var heartRate: Int = 0
    @Published var stepCount: Int = 0
    @Published var batteryLevel: Int = 0
    @Published var temperature: Double = 0.0
    
    // Constructor
    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
        
        if useMockData {
            startMockUpdates()
        }
    }
    
    // Initializer with context
    init(context: NSManagedObjectContext? = nil) {
        self.context = context
        
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)

        if useMockData {
            startMockUpdates()
        }
    }
    
    // Callback when Bluetooth state changes
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            // Scan for peripherals
            startScanning()
        }
    }
    
    // Begin scanning for peripherals
    func startScanning() {
        centralManager.scanForPeripherals(withServices: [espServiceUUID], options: nil)
        print("Scanning for ESP32...")
    }
    
    // Called when peripheral is discovered
    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String : Any],
                        rssi RSSI: NSNumber) {
        
        print("Found:", peripheral.name ?? "Unknown")
        discoveredPeripheral = peripheral
        
        centralManager.stopScan()
        centralManager.connect(peripheral, options: nil)
    }
    
    // Called when a connection to the peripheral is made
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        print("Connected to", peripheral.name ?? "ESP32")
        isConnected = true

        peripheral.delegate = self
        peripheral.discoverServices([espServiceUUID])
    }
    
    // Handle service discovery
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let services = peripheral.services {
            for service in services {
                print("Discovered Service:", service.uuid)
                
                peripheral.discoverCharacteristics(
                    [heartRateUUID, stepCountUUID, batteryUUID, temperatureUUID],
                    for: service
                )
            }
        }
    }
    
    // Handle characteristic discovery
    func peripheral(_ peripheral: CBPeripheral,
                    didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        if let characteristics = service.characteristics {
            for char in characteristics {
                print("Discovered characteristic:", char.uuid)
                // Subscribe to notifications
                peripheral.setNotifyValue(true, for: char)
            }
        }
    }
    
    // Handle characteristic updates
    func peripheral(_ peripheral: CBPeripheral,
                    didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard let data = characteristic.value else { return }

        switch characteristic.uuid { // Double check ESP32 metric return form
        case heartRateUUID: heartRate = Int(data[0])
        case stepCountUUID: stepCount = Int(data[0])
        case batteryUUID: batteryLevel = Int(data[0])
        case temperatureUUID: temperature = Double(data[0])
        default: break
        }
    }
    
    // Save user data (only past 7 days)
    func saveReading() {
        guard let context = context else { return }
        let newReading = HealthReading(context: context)
        
        newReading.timestamp = Date()
        newReading.heartRate = Int16(heartRate)
        newReading.stepCount = Int32(stepCount)
        newReading.batteryLevel = Int16(batteryLevel)
        newReading.temperature = temperature
        newReading.qualityFlags = "" // Set as needed
        
        // Set quality flags (add more flags for different validation conditions)
        if useMockData { newReading.qualityFlags = "Mock" }
        else { newReading.qualityFlags = "Original/Reliable"}
        
        do {
            try context.save()
        } catch {
            print("Failed to save HealthReadingData: \(error)")
        }
    }
    
    // Load last 7 days of data
    func loadReadings() -> [HealthReading] {
        guard let context = context else { return [] }
        let fetchRequest: NSFetchRequest<HealthReading> = HealthReading.fetchRequest()
        
        // Only last 7 days
        let sevenDaysAgo = Calendar.current.date(byAdding: .day, value: -7, to: Date())!
        fetchRequest.predicate = NSPredicate(format: "timestamp >= %@", sevenDaysAgo as NSDate)
        fetchRequest.sortDescriptors = [NSSortDescriptor(key: "timestamp", ascending: false)]
        
        do {
            return try context.fetch(fetchRequest)
        } catch {
            print("Failed to fetch HealthReadings: \(error)")
            return []
        }
    }
    
    // Mock data generator
    func generateMockData() {
        guard useMockData else { return }
        
        heartRate = Int.random(in: 60...100)
        stepCount = Int.random(in: 0...10000)
        batteryLevel = Int.random(in: 20...100)
        temperature = Double.random(in: 97.0...99.5)
        
        saveReading()
    }
    
    // Start mock data generation
    func startMockUpdates() {
        mockTimer?.invalidate()
        mockTimer = Timer.scheduledTimer(withTimeInterval: 10.0, repeats: true) { [weak self] _ in
            self?.generateMockData()
        }
    }
    
    // Stop mock data generation
    func stopMockUpdates() {
        mockTimer?.invalidate()
        mockTimer = nil
    }
    
    // Automatic reconnection logic
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        print("Disconnected")
        isConnected = false
        connectionError = error?.localizedDescription
  
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
            self.startScanning()
        }
    }

    // Write configuration to ESP32
    func sendConfiguration(samplingInterval: Int, notificationsEnabled: Bool) {
        guard let peripheral = discoveredPeripheral else { return }
        let configData = Data([UInt8(samplingInterval), notificationsEnabled ? 1 : 0])
        if let configChar = findCharacteristic(uuid: configUUID) {
            peripheral.writeValue(configData, for: configChar, type: .withResponse)
        }
    }

    // Helper to find characteristic by UUID
    func findCharacteristic(uuid: CBUUID) -> CBCharacteristic? {
        guard let services = discoveredPeripheral?.services else { return nil }
        for service in services {
            if let chars = service.characteristics {
                for char in chars where char.uuid == uuid {
                    return char
                }
            }
        }
        return nil
    }

    // Error handling for failed connections
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        print("Failed to connect: \(error?.localizedDescription ?? "Unknown error")")
        connectionError = error?.localizedDescription
        // Retry connection
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
            self.startScanning()
        }
    }

}
