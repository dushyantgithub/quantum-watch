import Foundation
import CoreBluetooth
import Combine
import os.log

private let bleLog = Logger(subsystem: "com.quantumwatch.companion", category: "BLE")

enum BLEConnectionState: String {
    case connected
    case scanning
    case disconnected
}

class BLEManager: NSObject, ObservableObject {
    static let serviceUUID = CBUUID(string: "0000AA00-0000-1000-8000-00805F9B34FB")
    static let audioCharUUID = CBUUID(string: "0000AA01-0000-1000-8000-00805F9B34FB")
    static let textCharUUID = CBUUID(string: "0000AA02-0000-1000-8000-00805F9B34FB")
    static let contextCharUUID = CBUUID(string: "0000AA03-0000-1000-8000-00805F9B34FB")
    static let notifCharUUID = CBUUID(string: "0000AA04-0000-1000-8000-00805F9B34FB")
    static let healthRequestCharUUID = CBUUID(string: "0000AA05-0000-1000-8000-00805F9B34FB")
    static let healthDataCharUUID = CBUUID(string: "0000AA06-0000-1000-8000-00805F9B34FB")

    private static let savedPeripheralKey = "com.quantumwatch.savedPeripheralUUID"

    @Published var connectionState: BLEConnectionState = .disconnected
    @Published var bluetoothReady: Bool = false

    var onAudioPacketReceived: ((Data) -> Void)?
    var onEndOfAudio: (() -> Void)?
    var onContextReady: (() -> Void)?
    var onHealthRefreshRequested: (() -> Void)?
    var onHealthChannelReady: (() -> Void)?

    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var textCharacteristic: CBCharacteristic?
    private var audioCharacteristic: CBCharacteristic?
    private var contextCharacteristic: CBCharacteristic?
    private var notifCharacteristic: CBCharacteristic?
    private var healthRequestCharacteristic: CBCharacteristic?
    private var healthDataCharacteristic: CBCharacteristic?
    private var autoReconnect = true
    /// Set by willRestoreState so centralManagerDidUpdateState can skip redundant work
    private var restoredFromBackground = false
    /// Pending notification payload to send when BLE reconnects
    private var pendingNotification: String?
    private var pendingContext: String?
    private var pendingHealthData: String?

    var healthChannelReady: Bool {
        peripheral != nil && (textCharacteristic != nil || healthDataCharacteristic != nil)
    }

    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: .main, options: [
            CBCentralManagerOptionRestoreIdentifierKey: "com.quantumwatch.companion.ble",
            CBCentralManagerOptionShowPowerAlertKey: true
        ])
    }

    // MARK: - Connection options

    /// Connect options — auto-reconnect keeps the link alive even when the
    /// app is fully suspended or terminated by the system.
    private let connectOptions: [String: Any] = [
        CBConnectPeripheralOptionNotifyOnDisconnectionKey: true,
        CBConnectPeripheralOptionEnableAutoReconnect: true
    ]

    // MARK: - Public API

    func startScanning() {
        guard centralManager.state == .poweredOn else { return }
        autoReconnect = true

        // If we already have a connected peripheral (e.g. restored from background), just ensure services
        if let existing = peripheral, existing.state == .connected {
            bleLog.info("Already connected to \(existing.identifier.uuidString.prefix(8)) — discovering services")
            connectionState = .connected
            savedPeripheralUUID = existing.identifier
            existing.discoverServices([BLEManager.serviceUUID])
            return
        }

        // If a connection is already pending, don't duplicate
        if let existing = peripheral, existing.state == .connecting {
            bleLog.info("Connection already pending to \(existing.identifier.uuidString.prefix(8))")
            connectionState = .scanning
            return
        }

        // Try direct reconnect to known peripheral
        if let saved = savedPeripheralUUID,
           let known = centralManager.retrievePeripherals(withIdentifiers: [saved]).first {
            bleLog.info("Reconnecting to saved peripheral \(saved.uuidString.prefix(8))")
            peripheral = known
            known.delegate = self
            connectionState = .scanning
            centralManager.connect(known, options: connectOptions)
            // Also scan in parallel in case the saved peripheral is gone
            centralManager.scanForPeripherals(
                withServices: [BLEManager.serviceUUID],
                options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
            )
            return
        }

        // Try retrieving already-connected peripherals (e.g. connected by another app)
        let alreadyConnected = centralManager.retrieveConnectedPeripherals(withServices: [BLEManager.serviceUUID])
        if let first = alreadyConnected.first {
            bleLog.info("Found already-connected peripheral \(first.identifier.uuidString.prefix(8))")
            peripheral = first
            first.delegate = self
            connectionState = .scanning
            centralManager.connect(first, options: connectOptions)
            return
        }

        // Fall back to scanning
        bleLog.info("No known peripheral — scanning for service")
        connectionState = .scanning
        centralManager.scanForPeripherals(
            withServices: [BLEManager.serviceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
    }

    func stopScanning() {
        centralManager.stopScan()
        if connectionState == .scanning {
            connectionState = .disconnected
        }
    }

    func disconnect() {
        autoReconnect = false
        if let peripheral = peripheral {
            centralManager.cancelPeripheralConnection(peripheral)
        }
        centralManager.stopScan()
        connectionState = .disconnected
    }

    func writeNotification(_ payload: String) {
        guard let peripheral = peripheral, peripheral.state == .connected else {
            bleLog.warning("writeNotification(\(payload)): not connected — queuing")
            pendingNotification = payload
            return
        }
        guard let characteristic = notifCharacteristic else {
            bleLog.warning("writeNotification(\(payload)): characteristic not discovered — queuing")
            pendingNotification = payload
            return
        }
        bleLog.info("writeNotification(\(payload)): writing to watch")
        pendingNotification = nil
        let data = Data(payload.utf8)
        peripheral.writeValue(data, for: characteristic, type: .withResponse)
    }

    /// Flush any queued notification after BLE reconnects and characteristics are discovered
    private func flushPendingNotification() {
        guard let payload = pendingNotification else { return }
        bleLog.info("Flushing pending notification: \(payload)")
        writeNotification(payload)
    }

    func writeContext(_ payload: String) {
        guard let peripheral = peripheral, peripheral.state == .connected else {
            bleLog.warning("writeContext: not connected, queuing \(payload, privacy: .public)")
            pendingContext = payload
            return
        }
        guard let characteristic = contextCharacteristic else {
            bleLog.warning("writeContext: characteristic not ready, queuing \(payload, privacy: .public)")
            pendingContext = payload
            return
        }
        bleLog.info("writeContext: sending \(payload, privacy: .public)")
        pendingContext = nil
        let data = Data(payload.utf8)
        peripheral.writeValue(data, for: characteristic, type: .withResponse)
    }

    func writeResponse(_ text: String) {
        guard let peripheral = peripheral,
              let characteristic = textCharacteristic else { return }

        writeChunkedText(text, to: characteristic, on: peripheral)
    }

    func writeHealthData(_ text: String) {
        let payload = "HEALTH|\(text)"

        guard let peripheral = peripheral, peripheral.state == .connected else {
            bleLog.warning("writeHealthData: not connected, queuing")
            pendingHealthData = payload
            return
        }
        if let characteristic = textCharacteristic {
            bleLog.info("Sending health payload to watch over text characteristic")
            pendingHealthData = nil
            writeChunkedText(payload, to: characteristic, on: peripheral)
            return
        }
        guard let characteristic = healthDataCharacteristic else {
            bleLog.warning("writeHealthData: no compatible characteristic ready, queuing")
            pendingHealthData = payload
            return
        }

        bleLog.info("Sending health payload to watch over health characteristic")
        pendingHealthData = nil
        writeChunkedText(payload, to: characteristic, on: peripheral)
    }

    private func writeChunkedText(_ text: String, to characteristic: CBCharacteristic, on peripheral: CBPeripheral) {
        let data = Data(text.utf8)
        let mtu = peripheral.maximumWriteValueLength(for: .withResponse)
        let chunkSize = max(mtu - 1, 20) // 1 byte for flags

        if data.count <= chunkSize {
            var packet = Data([0x01]) // final flag
            packet.append(data)
            peripheral.writeValue(packet, for: characteristic, type: .withResponse)
        } else {
            var offset = 0
            while offset < data.count {
                let remaining = data.count - offset
                let length = min(chunkSize, remaining)
                let isFinal = (offset + length) >= data.count

                var packet = Data([isFinal ? 0x01 : 0x00])
                packet.append(data[offset..<(offset + length)])
                peripheral.writeValue(packet, for: characteristic, type: .withResponse)
                offset += length
            }
        }
    }

    private func notifyHealthChannelReadyIfPossible() {
        guard healthChannelReady else { return }
        bleLog.info("Health data channel ready")
        flushPendingHealthData()
        onHealthChannelReady?()
    }

    private func flushPendingContext() {
        guard let payload = pendingContext else { return }
        bleLog.info("Flushing pending context")
        writeContext(payload)
    }

    private func flushPendingHealthData() {
        guard let payload = pendingHealthData else { return }
        bleLog.info("Flushing pending health payload")
        writeHealthData(payload)
    }

    // MARK: - Peripheral Persistence

    private var savedPeripheralUUID: UUID? {
        get {
            guard let str = UserDefaults.standard.string(forKey: BLEManager.savedPeripheralKey) else { return nil }
            return UUID(uuidString: str)
        }
        set {
            UserDefaults.standard.set(newValue?.uuidString, forKey: BLEManager.savedPeripheralKey)
        }
    }
}

// MARK: - CBCentralManagerDelegate
extension BLEManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        bleLog.info("Central state: \(central.state.rawValue)")
        bluetoothReady = (central.state == .poweredOn)
        if central.state == .poweredOn {
            if restoredFromBackground {
                bleLog.info("Restored from background — checking peripheral state")
                restoredFromBackground = false
                // willRestoreState already set up the peripheral; just ensure connection
                if let p = peripheral {
                    if p.state == .connected {
                        connectionState = .connected
                        p.discoverServices([BLEManager.serviceUUID])
                    } else if p.state != .connecting {
                        centralManager.connect(p, options: connectOptions)
                        connectionState = .scanning
                    }
                } else {
                    startScanning()
                }
            } else {
                startScanning()
            }
        } else {
            connectionState = .disconnected
        }
    }

    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        bleLog.info("willRestoreState called")
        restoredFromBackground = true

        if let peripherals = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
           let restored = peripherals.first {
            bleLog.info("Restored peripheral \(restored.identifier.uuidString.prefix(8)), state=\(restored.state.rawValue)")
            peripheral = restored
            peripheral?.delegate = self
            savedPeripheralUUID = restored.identifier
            // Don't call connect/discoverServices here — wait for centralManagerDidUpdateState
        }

        // Restore any pending scan
        if let services = dict[CBCentralManagerRestoredStateScanServicesKey] as? [CBUUID] {
            bleLog.info("Restored scan for services: \(services)")
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                         advertisementData: [String: Any], rssi RSSI: NSNumber) {
        bleLog.info("Discovered peripheral \(peripheral.identifier.uuidString.prefix(8)) RSSI=\(RSSI)")

        // If we're already connecting to a saved peripheral, ignore scan results
        if let existing = self.peripheral, existing.state == .connecting {
            if existing.identifier == peripheral.identifier { return }
        }

        self.peripheral = peripheral
        peripheral.delegate = self
        central.stopScan()
        central.connect(peripheral, options: connectOptions)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        bleLog.info("Connected to \(peripheral.identifier.uuidString.prefix(8))")
        central.stopScan()
        connectionState = .connected
        autoReconnect = true
        savedPeripheralUUID = peripheral.identifier
        peripheral.discoverServices([BLEManager.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                         timestamp: CFAbsoluteTime, isReconnecting: Bool, error: Error?) {
        bleLog.warning("Disconnected from \(peripheral.identifier.uuidString.prefix(8)), isReconnecting=\(isReconnecting), error: \(error?.localizedDescription ?? "none")")
        clearCharacteristics()

        if isReconnecting {
            // System is handling reconnection via CBConnectPeripheralOptionEnableAutoReconnect.
            // Do NOT call connect() again — just wait for didConnect.
            connectionState = .scanning
        } else {
            connectionState = .disconnected
            guard autoReconnect else { return }

            // Manually queue reconnection — CoreBluetooth maintains this request
            // even when the app is suspended
            bleLog.info("Queuing background reconnect")
            central.connect(peripheral, options: connectOptions)
            connectionState = .scanning

            // Also scan as fallback (only effective while app is active)
            DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { [weak self] in
                guard let self = self, self.connectionState != .connected else { return }
                self.centralManager.scanForPeripherals(
                    withServices: [BLEManager.serviceUUID],
                    options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
                )
            }
        }
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        bleLog.error("Failed to connect: \(error?.localizedDescription ?? "unknown")")
        connectionState = .disconnected

        guard autoReconnect else { return }

        // Retry connection
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) { [weak self] in
            guard let self = self else { return }
            self.centralManager.connect(peripheral, options: self.connectOptions)
            self.connectionState = .scanning
        }
    }

    private func clearCharacteristics() {
        textCharacteristic = nil
        audioCharacteristic = nil
        contextCharacteristic = nil
        notifCharacteristic = nil
        healthRequestCharacteristic = nil
        healthDataCharacteristic = nil
    }
}

// MARK: - CBPeripheralDelegate
extension BLEManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            bleLog.error("Service discovery error: \(error.localizedDescription)")
            return
        }
        guard let services = peripheral.services else { return }
        for service in services {
            if service.uuid == BLEManager.serviceUUID {
                peripheral.discoverCharacteristics(
                    [
                        BLEManager.audioCharUUID,
                        BLEManager.textCharUUID,
                        BLEManager.contextCharUUID,
                        BLEManager.notifCharUUID,
                        BLEManager.healthRequestCharUUID,
                        BLEManager.healthDataCharUUID,
                    ],
                    for: service
                )
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error = error {
            bleLog.error("Characteristic discovery error: \(error.localizedDescription)")
            return
        }
        guard let characteristics = service.characteristics else { return }
        bleLog.info("Discovered \(characteristics.count) characteristics")
        for char in characteristics {
            switch char.uuid {
            case BLEManager.audioCharUUID:
                audioCharacteristic = char
                peripheral.setNotifyValue(true, for: char)
            case BLEManager.textCharUUID:
                textCharacteristic = char
                notifyHealthChannelReadyIfPossible()
            case BLEManager.contextCharUUID:
                contextCharacteristic = char
                flushPendingContext()
                onContextReady?()
            case BLEManager.notifCharUUID:
                notifCharacteristic = char
                bleLog.info("Notification characteristic ready")
                flushPendingNotification()
            case BLEManager.healthRequestCharUUID:
                healthRequestCharacteristic = char
                bleLog.info("Health request characteristic ready; enabling notify")
                peripheral.setNotifyValue(true, for: char)
            case BLEManager.healthDataCharUUID:
                healthDataCharacteristic = char
                bleLog.info("Health data characteristic ready")
                notifyHealthChannelReadyIfPossible()
            default:
                break
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            bleLog.error("Write to \(characteristic.uuid) failed: \(error.localizedDescription)")
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let data = characteristic.value else { return }

        if characteristic.uuid == BLEManager.healthRequestCharUUID {
            if data.count == 1 && data[0] == UInt8(ascii: "R") {
                bleLog.info("Health refresh requested by watch")
                onHealthRefreshRequested?()
            }
            return
        }

        guard characteristic.uuid == BLEManager.audioCharUUID else { return }

        // Ignore 1-byte keepalive pings from watch (0xFE)
        if data.count == 1 && data[0] == 0xFE {
            return
        }

        // Check for end-of-audio marker
        if data.count == 4 && data == Data([0xFF, 0xFF, 0xFF, 0xFF]) {
            onEndOfAudio?()
        } else {
            onAudioPacketReceived?(data)
        }
    }
}
