import Foundation
import CallKit
import os.log

private let logger = Logger(subsystem: "com.quantumwatch.companion", category: "CallObserver")

/// Observes incoming phone call state and sends notifications to the watch via BLE.
class CallObserverService: NSObject, CXCallObserverDelegate {
    private let callObserver = CXCallObserver()
    private let bleManager: BLEManager
    /// Dedicated queue so callbacks are delivered even when main queue is frozen in background
    private let callQueue = DispatchQueue(label: "com.quantumwatch.companion.callObserver")

    /// Track calls we've seen so we can detect missed calls
    private var activeCallUUIDs: Set<UUID> = []
    private var connectedCallUUIDs: Set<UUID> = []

    init(bleManager: BLEManager) {
        self.bleManager = bleManager
        super.init()
        callObserver.setDelegate(self, queue: callQueue)
        logger.info("CallObserverService initialized")
    }

    func callObserver(_ callObserver: CXCallObserver, callChanged call: CXCall) {
        let uuid = call.uuid
        logger.info("Call changed: outgoing=\(call.isOutgoing) connected=\(call.hasConnected) ended=\(call.hasEnded) uuid=\(uuid.uuidString.prefix(8))")

        if !call.isOutgoing && !call.hasConnected && !call.hasEnded {
            // Incoming call ringing
            if !activeCallUUIDs.contains(uuid) {
                activeCallUUIDs.insert(uuid)
                logger.info("Sending CI (call incoming)")
                sendNotification("CI")
            }
        }

        if call.hasConnected {
            connectedCallUUIDs.insert(uuid)
            // Call was answered — dismiss notification on watch
            if activeCallUUIDs.contains(uuid) {
                logger.info("Sending CE (call answered)")
                sendNotification("CE")
            }
        }

        if call.hasEnded {
            if activeCallUUIDs.contains(uuid) && !connectedCallUUIDs.contains(uuid) {
                // Incoming call ended without being answered = missed call
                logger.info("Sending CM (call missed)")
                sendNotification("CM")
            } else if activeCallUUIDs.contains(uuid) && connectedCallUUIDs.contains(uuid) {
                // Call ended after being answered
                logger.info("Sending CE (call ended)")
                sendNotification("CE")
            }
            activeCallUUIDs.remove(uuid)
            connectedCallUUIDs.remove(uuid)
        }
    }

    /// Dispatch BLE write to main queue (BLEManager operates on main)
    private func sendNotification(_ payload: String) {
        DispatchQueue.main.async { [weak self] in
            self?.bleManager.writeNotification(payload)
        }
    }
}
