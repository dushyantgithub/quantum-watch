import Foundation
import HealthKit
import os.log
import UIKit

private let healthLog = Logger(subsystem: "com.quantumwatch.companion", category: "Health")

final class HealthSyncService {
    private let bleManager: BLEManager
    private let healthStore = HKHealthStore()
    private var authorizationResolved = false
    private var authorizationGranted = false
    private var observerQueriesStarted = false
    private var syncTimer: Timer?
    private var appDidBecomeActiveObserver: NSObjectProtocol?

    init(bleManager: BLEManager) {
        self.bleManager = bleManager
        bleManager.onHealthRefreshRequested = { [weak self] in
            Task {
                await self?.handleRefreshRequest()
            }
        }
        bleManager.onHealthChannelReady = { [weak self] in
            Task {
                await self?.pushSnapshotIfPossible(reason: "channel ready")
            }
        }
        appDidBecomeActiveObserver = NotificationCenter.default.addObserver(
            forName: UIApplication.didBecomeActiveNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            Task {
                await self?.pushSnapshotIfPossible(reason: "app became active")
            }
        }
    }

    deinit {
        syncTimer?.invalidate()
        if let observer = appDidBecomeActiveObserver {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    func primeAuthorizationIfNeeded() {
        Task {
            let granted = await requestAuthorizationIfNeeded()
            if granted {
                await self.startAutomaticSyncIfNeeded()
                await self.pushSnapshotIfPossible(reason: "authorization primed")
            }
        }
    }

    private func handleRefreshRequest() async {
        healthLog.info("Preparing today's Apple Health snapshot")
        guard HKHealthStore.isHealthDataAvailable() else {
            bleManager.writeHealthData("Unavailable|Unavailable|Unavailable|Unavailable|Health Off")
            return
        }

        let authorized = await requestAuthorizationIfNeeded()
        guard authorized else {
            bleManager.writeHealthData("Permission|Permission|Permission|Permission|Open iPhone app")
            return
        }

        do {
            let snapshot = try await fetchSnapshot()
            bleManager.writeHealthData(snapshot)
        } catch {
            healthLog.error("Health fetch failed: \(error.localizedDescription)")
            bleManager.writeHealthData("Error|Error|Error|Error|Fetch failed")
        }
    }

    private func pushSnapshotIfPossible(reason: String) async {
        guard bleManager.healthChannelReady else {
            healthLog.info("Skipping health push; channel not ready")
            return
        }
        guard HKHealthStore.isHealthDataAvailable() else {
            return
        }

        let authorized = await requestAuthorizationIfNeeded()
        guard authorized else {
            healthLog.info("Skipping health push; permission not granted")
            return
        }

        do {
            let snapshot = try await fetchSnapshot()
            healthLog.info("Pushing today's Health snapshot: \(reason, privacy: .public)")
            bleManager.writeHealthData(snapshot)
        } catch {
            healthLog.error("Background health push failed: \(error.localizedDescription)")
        }
    }

    @MainActor
    private func startAutomaticSyncIfNeeded() async {
        if syncTimer == nil {
            syncTimer = Timer.scheduledTimer(withTimeInterval: 300, repeats: true) { [weak self] _ in
                Task {
                    await self?.pushSnapshotIfPossible(reason: "periodic sync")
                }
            }
        }
        await startObserverQueriesIfNeeded()
    }

    private func requestAuthorizationIfNeeded() async -> Bool {
        if authorizationResolved {
            return authorizationGranted
        }

        guard
            let stepType = HKQuantityType.quantityType(forIdentifier: .stepCount),
            let energyType = HKQuantityType.quantityType(forIdentifier: .activeEnergyBurned),
            let distanceType = HKQuantityType.quantityType(forIdentifier: .distanceWalkingRunning),
            let heartRateType = HKQuantityType.quantityType(forIdentifier: .heartRate)
        else {
            authorizationResolved = true
            authorizationGranted = false
            return false
        }

        let readTypes: Set<HKObjectType> = [stepType, energyType, distanceType, heartRateType]
        let granted = await withCheckedContinuation { continuation in
            healthStore.requestAuthorization(toShare: nil, read: readTypes) { success, error in
                if let error {
                    healthLog.error("Health authorization failed: \(error.localizedDescription)")
                }
                continuation.resume(returning: success)
            }
        }

        authorizationResolved = true
        authorizationGranted = granted
        healthLog.info("Health authorization resolved: granted=\(granted)")
        return granted
    }

    @MainActor
    private func startObserverQueriesIfNeeded() async {
        if observerQueriesStarted {
            return
        }

        guard
            let stepType = HKQuantityType.quantityType(forIdentifier: .stepCount),
            let energyType = HKQuantityType.quantityType(forIdentifier: .activeEnergyBurned),
            let distanceType = HKQuantityType.quantityType(forIdentifier: .distanceWalkingRunning),
            let heartRateType = HKQuantityType.quantityType(forIdentifier: .heartRate)
        else {
            return
        }

        observerQueriesStarted = true

        for type in [stepType, energyType, distanceType, heartRateType] {
            let query = HKObserverQuery(sampleType: type, predicate: nil) { [weak self] _, completion, error in
                if let error {
                    healthLog.error("Health observer error: \(error.localizedDescription)")
                    completion()
                    return
                }
                Task {
                    await self?.pushSnapshotIfPossible(reason: "health observer")
                    completion()
                }
            }
            healthStore.execute(query)
            healthStore.enableBackgroundDelivery(for: type, frequency: .immediate) { success, error in
                if let error {
                    healthLog.error("Background delivery enable failed: \(error.localizedDescription)")
                } else {
                    healthLog.info("Background delivery enabled for \(type.identifier, privacy: .public): \(success)")
                }
            }
        }
    }

    private func fetchSnapshot() async throws -> String {
        let now = Date()
        let startOfDay = Calendar.current.startOfDay(for: now)

        guard
            let stepType = HKQuantityType.quantityType(forIdentifier: .stepCount),
            let energyType = HKQuantityType.quantityType(forIdentifier: .activeEnergyBurned),
            let distanceType = HKQuantityType.quantityType(forIdentifier: .distanceWalkingRunning),
            let heartRateType = HKQuantityType.quantityType(forIdentifier: .heartRate)
        else {
            throw HealthSyncError.missingType
        }

        async let steps = cumulativeSum(for: stepType, unit: .count(), start: startOfDay, end: now)
        async let energy = cumulativeSum(for: energyType, unit: .kilocalorie(), start: startOfDay, end: now)
        async let distance = cumulativeSum(for: distanceType, unit: .meter(), start: startOfDay, end: now)
        async let heartRate = latestQuantity(
            for: heartRateType,
            unit: HKUnit.count().unitDivided(by: .minute()),
            start: startOfDay,
            end: now
        )

        let stepsValue = try await steps
        let energyValue = try await energy
        let distanceValue = try await distance
        let heartRateValue = try await heartRate

        let formattedSteps = NumberFormatter.localizedString(from: NSNumber(value: Int(stepsValue.rounded())), number: .decimal)
        let formattedEnergy = "\(Int(energyValue.rounded())) kcal"
        let formattedDistance = String(format: "%.1f km", distanceValue / 1000.0)
        let formattedHeartRate: String
        if let heartRateValue {
            formattedHeartRate = "\(Int(heartRateValue.rounded())) bpm"
        } else {
            formattedHeartRate = "--"
        }
        let formattedTime = DateFormatter.localizedString(from: now, dateStyle: .medium, timeStyle: .short)

        return [formattedSteps, formattedEnergy, formattedDistance, formattedHeartRate, formattedTime].joined(separator: "|")
    }

    private func cumulativeSum(
        for type: HKQuantityType,
        unit: HKUnit,
        start: Date,
        end: Date
    ) async throws -> Double {
        try await withCheckedThrowingContinuation { continuation in
            let predicate = HKQuery.predicateForSamples(withStart: start, end: end, options: .strictStartDate)
            let query = HKStatisticsQuery(quantityType: type, quantitySamplePredicate: predicate, options: .cumulativeSum) {
                _, statistics, error in
                if let error {
                    continuation.resume(throwing: error)
                    return
                }
                let value = statistics?.sumQuantity()?.doubleValue(for: unit) ?? 0
                continuation.resume(returning: value)
            }
            healthStore.execute(query)
        }
    }

    private func latestQuantity(
        for type: HKQuantityType,
        unit: HKUnit,
        start: Date,
        end: Date
    ) async throws -> Double? {
        try await withCheckedThrowingContinuation { continuation in
            let predicate = HKQuery.predicateForSamples(withStart: start, end: end, options: .strictEndDate)
            let sortDescriptor = NSSortDescriptor(key: HKSampleSortIdentifierEndDate, ascending: false)
            let query = HKSampleQuery(sampleType: type, predicate: predicate, limit: 1, sortDescriptors: [sortDescriptor]) {
                _, samples, error in
                if let error {
                    continuation.resume(throwing: error)
                    return
                }
                let sample = samples?.first as? HKQuantitySample
                let value = sample?.quantity.doubleValue(for: unit)
                continuation.resume(returning: value)
            }
            healthStore.execute(query)
        }
    }
}

private enum HealthSyncError: Error {
    case missingType
}
