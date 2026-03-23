import SwiftUI

@main
struct QuantumWatchCompanionApp: App {
    @StateObject private var viewModel = VoiceAssistantViewModel()
    @StateObject private var themeManager = ThemeManager()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(viewModel)
                .environmentObject(themeManager)
                .preferredColorScheme(themeManager.currentTheme.colorScheme)
        }
    }
}
