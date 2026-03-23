import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var viewModel: VoiceAssistantViewModel
    @EnvironmentObject var themeManager: ThemeManager
    @Environment(\.dismiss) var dismiss
    @Environment(\.colorScheme) var colorScheme

    @State private var openAIKey: String = ""
    @State private var whisperKey: String = ""
    @State private var selectedModel: AIModel = .gpt4oMini
    @State private var useAppleSpeech: Bool = true

    private var theme: ThemeColors {
        ThemeColors.forScheme(colorScheme)
    }

    var body: some View {
        NavigationStack {
            Form {
                Section {
                    Picker("Appearance", selection: $themeManager.currentTheme) {
                        ForEach(AppTheme.allCases, id: \.self) { theme in
                            Text(theme.displayName).tag(theme)
                        }
                    }
                    .pickerStyle(.segmented)
                } header: {
                    Label("Theme", systemImage: "paintbrush.fill")
                        .font(.QW.overline)
                        .textCase(.uppercase)
                        .foregroundColor(self.theme.accent)
                }

                Section {
                    Picker("Speech Engine", selection: $useAppleSpeech) {
                        Text("Apple Speech").tag(true)
                        Text("OpenAI Whisper").tag(false)
                    }
                    .pickerStyle(.segmented)

                    if !useAppleSpeech {
                        SecureField("Whisper API Key", text: $whisperKey)
                            .textContentType(.password)
                            .autocorrectionDisabled()
                    }
                } header: {
                    Label("Speech Recognition", systemImage: "waveform")
                        .font(.QW.overline)
                        .textCase(.uppercase)
                        .foregroundColor(theme.accent)
                }

                Section {
                    SecureField("OpenAI API Key", text: $openAIKey)
                        .textContentType(.password)
                        .autocorrectionDisabled()

                    Picker("Model", selection: $selectedModel) {
                        ForEach(AIModel.allCases, id: \.self) { model in
                            Text(model.displayName).tag(model)
                        }
                    }

                    Text("Leave API key empty to return transcription only (no AI processing)")
                        .font(.QW.caption)
                        .foregroundColor(theme.textTertiary)
                } header: {
                    Label("AI Response", systemImage: "brain")
                        .font(.QW.overline)
                        .textCase(.uppercase)
                        .foregroundColor(theme.accent)
                }

                Section {
                    HStack {
                        Circle()
                            .fill(connectionDotColor)
                            .frame(width: 10, height: 10)
                            .shadow(color: connectionDotColor.opacity(0.6), radius: 3)

                        Text(connectionStatusText)
                            .font(.QW.label1)

                        Spacer()

                        if viewModel.bleManager.connectionState == .scanning {
                            ProgressView()
                                .tint(theme.accent)
                                .scaleEffect(0.8)
                        }
                    }

                    if !viewModel.bleManager.bluetoothReady {
                        HStack(spacing: 6) {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundColor(.orange)
                                .font(.QW.caption)
                            Text("Bluetooth is off. Enable it in System Settings.")
                                .font(.QW.caption)
                                .foregroundColor(theme.textTertiary)
                        }
                    }

                    if viewModel.bleManager.connectionState == .disconnected && viewModel.bleManager.bluetoothReady {
                        Button {
                            viewModel.bleManager.startScanning()
                        } label: {
                            HStack {
                                Spacer()
                                Text("Reconnect")
                                    .font(.QW.label1)
                                    .fontWeight(.medium)
                                Spacer()
                            }
                        }
                    }

                    Text("Auto-connects in the background when both devices are on")
                        .font(.QW.caption)
                        .foregroundColor(theme.textTertiary)
                } header: {
                    Label("Connection", systemImage: "antenna.radiowaves.left.and.right")
                        .font(.QW.overline)
                        .textCase(.uppercase)
                        .foregroundColor(theme.accent)
                }

                Section {
                    HStack {
                        Text("Version")
                            .font(.QW.label1)
                        Spacer()
                        Text("1.0.0")
                            .font(.QW.label2)
                            .foregroundColor(theme.textTertiary)
                    }
                } header: {
                    Label("About", systemImage: "info.circle")
                        .font(.QW.overline)
                        .textCase(.uppercase)
                        .foregroundColor(theme.accent)
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Button("Cancel") { dismiss() }
                }
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Save") {
                        saveSettings()
                        dismiss()
                    }
                    .fontWeight(.semibold)
                    .foregroundColor(theme.accent)
                }
            }
            .onAppear {
                loadSettings()
            }
        }
    }

    private var connectionDotColor: Color {
        switch viewModel.bleManager.connectionState {
        case .connected: return Color.QW.green200
        case .scanning: return Color.QW.gold200
        case .disconnected: return Color.QW.blueGray550
        }
    }

    private var connectionStatusText: String {
        switch viewModel.bleManager.connectionState {
        case .connected: return "Connected to QuantumWatch"
        case .scanning: return "Scanning for devices..."
        case .disconnected: return "Disconnected"
        }
    }

    private func loadSettings() {
        openAIKey = KeychainHelper.load(key: "openai_api_key") ?? ""
        whisperKey = KeychainHelper.load(key: "whisper_api_key") ?? ""
        selectedModel = AIModel(rawValue: UserDefaults.standard.string(forKey: "ai_model") ?? "") ?? .gpt4oMini
        useAppleSpeech = UserDefaults.standard.object(forKey: "use_apple_speech") as? Bool ?? true
    }

    private func saveSettings() {
        KeychainHelper.save(key: "openai_api_key", value: openAIKey)
        KeychainHelper.save(key: "whisper_api_key", value: whisperKey)
        UserDefaults.standard.set(selectedModel.rawValue, forKey: "ai_model")
        UserDefaults.standard.set(useAppleSpeech, forKey: "use_apple_speech")
        viewModel.reloadSettings()
    }
}

enum AIModel: String, CaseIterable {
    case gpt4oMini = "gpt-4o-mini"
    case gpt4o = "gpt-4o"
    case gpt4Turbo = "gpt-4-turbo"

    var displayName: String {
        switch self {
        case .gpt4oMini: return "GPT-4o Mini"
        case .gpt4o: return "GPT-4o"
        case .gpt4Turbo: return "GPT-4 Turbo"
        }
    }
}
