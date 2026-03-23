import Foundation
import CoreLocation

/// Fetches time, location, and weather from the iPhone and sends to the watch via BLE.
/// Runs in the background using CoreLocation's significant change monitoring.
class ContextSyncService: NSObject, ObservableObject, CLLocationManagerDelegate {
    private let bleManager: BLEManager
    private let locationManager = CLLocationManager()
    private var syncTimer: Timer?
    private var lastLocation: CLLocation?
    private var lastLocationName: String = "iPhone"
    private var lastTemp: Int = 0
    private var hasWeather = false

    init(bleManager: BLEManager) {
        self.bleManager = bleManager
        super.init()

        locationManager.delegate = self
        locationManager.desiredAccuracy = kCLLocationAccuracyKilometer
        locationManager.allowsBackgroundLocationUpdates = true
        locationManager.pausesLocationUpdatesAutomatically = false

        bleManager.onContextReady = { [weak self] in
            self?.onConnected()
        }
    }

    func start() {
        locationManager.requestAlwaysAuthorization()
        locationManager.startMonitoringSignificantLocationChanges()
        startSyncTimer()
    }

    private func onConnected() {
        // Always push current time immediately on BLE connect, even if location/weather is still pending.
        sendContext()
        // Also trigger a fresh location update
        locationManager.requestLocation()
    }

    private func startSyncTimer() {
        syncTimer?.invalidate()
        // Keep time/date fresh every minute; request weather/location less expensively from the same tick.
        syncTimer = Timer.scheduledTimer(withTimeInterval: 60, repeats: true) { [weak self] tick in
            guard let self = self else { return }
            self.sendContext()
            if Int(tick.fireDate.timeIntervalSince1970) % 300 < 60 {
                self.locationManager.requestLocation()
            }
        }
        // Immediate baseline sync for time/date, then request a fresher location/weather.
        sendContext()
        locationManager.requestLocation()
    }

    private func sendContext() {
        // The watch currently displays localtime from the raw epoch without a timezone config,
        // so send local wall-clock epoch seconds instead of UTC.
        let now = Date()
        let timestamp = Int(now.timeIntervalSince1970) + TimeZone.current.secondsFromGMT(for: now)
        let payload = "\(timestamp)|\(lastTemp)|\(lastLocationName)"
        bleManager.writeContext(payload)
    }

    // MARK: - Weather Fetch (Open-Meteo, free, no API key)

    private func fetchWeather(for location: CLLocation) {
        let lat = location.coordinate.latitude
        let lon = location.coordinate.longitude

        // Reverse geocode for location name
        let geocoder = CLGeocoder()
        geocoder.reverseGeocodeLocation(location) { [weak self] placemarks, _ in
            if let place = placemarks?.first {
                self?.lastLocationName = place.locality ?? place.administrativeArea ?? "Unknown"
                self?.sendContext()
            }
        }

        // Fetch weather from Open-Meteo
        let urlString = "https://api.open-meteo.com/v1/forecast?latitude=\(lat)&longitude=\(lon)&current=temperature_2m&timezone=auto"
        guard let url = URL(string: urlString) else { return }

        URLSession.shared.dataTask(with: url) { [weak self] data, _, error in
            guard let self = self, let data = data, error == nil else { return }

            if let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
               let current = json["current"] as? [String: Any],
               let temp = current["temperature_2m"] as? Double {
                DispatchQueue.main.async {
                    self.lastTemp = Int(temp.rounded())
                    self.hasWeather = true
                    self.sendContext()
                }
            }
        }.resume()
    }

    // MARK: - CLLocationManagerDelegate

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        guard let location = locations.last else { return }
        lastLocation = location
        fetchWeather(for: location)
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        // Always keep watch time/date moving even if location/weather fails.
        sendContext()
    }

    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        if manager.authorizationStatus == .authorizedAlways ||
           manager.authorizationStatus == .authorizedWhenInUse {
            manager.startMonitoringSignificantLocationChanges()
            manager.requestLocation()
        }
    }
}
