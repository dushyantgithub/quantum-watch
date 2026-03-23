import Foundation
import Combine

struct ActivityEntry: Identifiable {
    let id = UUID()
    let message: String
    let timestamp: Date
    let icon: String
    let color: SwiftUI.Color

    init(message: String, icon: String = "circle.fill", color: SwiftUI.Color = .gray) {
        self.message = message
        self.timestamp = Date()
        self.icon = icon
        self.color = color
    }
}

enum ChatRole {
    case user
    case assistant
}

struct ChatMessage: Identifiable {
    let id = UUID()
    let role: ChatRole
    let text: String
    let timestamp: Date

    init(role: ChatRole, text: String) {
        self.role = role
        self.text = text
        self.timestamp = Date()
    }
}

import SwiftUI

@MainActor
class VoiceAssistantViewModel: ObservableObject {
    @Published var bleManager = BLEManager()
    @Published var activityLog: [ActivityEntry] = []
    @Published var messages: [ChatMessage] = []
    @Published var currentStatus: String?
    @Published var isProcessing = false

    private let audioDecoder = AudioDecoder()
    private(set) var contextSync: ContextSyncService!
    private(set) var callObserver: CallObserverService!
    private(set) var healthSync: HealthSyncService!
    private var useAppleSpeech = true
    private var openAIKey = ""
    private var whisperKey = ""
    private var aiModel = "gpt-4o-mini"

    init() {
        loadSettings()
        setupBLECallbacks()
        healthSync = HealthSyncService(bleManager: bleManager)
        healthSync.primeAuthorizationIfNeeded()
        contextSync = ContextSyncService(bleManager: bleManager)
        contextSync.start()
        callObserver = CallObserverService(bleManager: bleManager)
    }

    func reloadSettings() {
        loadSettings()
    }

    private func loadSettings() {
        openAIKey = KeychainHelper.load(key: "openai_api_key") ?? ""
        whisperKey = KeychainHelper.load(key: "whisper_api_key") ?? ""
        aiModel = UserDefaults.standard.string(forKey: "ai_model") ?? "gpt-4o-mini"
        useAppleSpeech = UserDefaults.standard.object(forKey: "use_apple_speech") as? Bool ?? true
    }

    private func setupBLECallbacks() {
        bleManager.onAudioPacketReceived = { [weak self] data in
            guard let self = self else { return }
            self.audioDecoder.addPacket(data)

            // Update status on first packet
            if self.audioDecoder.packetCount == 1 {
                Task { @MainActor in
                    self.currentStatus = "Receiving audio..."
                    self.isProcessing = true
                    self.addLog("Receiving audio from watch...", icon: "waveform", color: .blue)
                }
            }
        }

        bleManager.onEndOfAudio = { [weak self] in
            guard let self = self else { return }
            Task { @MainActor in
                await self.processAudio()
            }
        }
    }

    private func processAudio() async {
        guard audioDecoder.hasData else {
            addLog("No audio data received", icon: "exclamationmark.triangle", color: .yellow)
            currentStatus = nil
            isProcessing = false
            return
        }

        addLog("Received \(audioDecoder.packetCount) audio packets", icon: "checkmark.circle", color: .green)

        // Decode ADPCM to PCM
        currentStatus = "Decoding audio..."
        let pcmData = audioDecoder.decodeAll()
        addLog("Decoded \(pcmData.count) bytes of PCM audio", icon: "waveform.circle", color: .blue)

        // Transcribe
        currentStatus = "Transcribing..."
        addLog("Transcribing audio...", icon: "text.bubble", color: .orange)

        do {
            let speechService: SpeechService
            if useAppleSpeech {
                speechService = AppleSpeechService()
            } else {
                guard !whisperKey.isEmpty else {
                    addLog("Whisper API key not set", icon: "exclamationmark.triangle", color: .red)
                    resetState()
                    return
                }
                speechService = WhisperService(apiKey: whisperKey)
            }

            let transcription = try await speechService.transcribe(pcmData: pcmData, sampleRate: 16000)
            addLog("Transcription: \(transcription)", icon: "text.quote", color: .white)

            // Add user message to chat
            messages.append(ChatMessage(role: .user, text: transcription))

            // AI processing
            var responseText = transcription
            if !openAIKey.isEmpty {
                currentStatus = "Getting AI response..."
                addLog("Sending to AI...", icon: "brain", color: .purple)

                let aiService = AIService(apiKey: openAIKey, model: aiModel)
                responseText = try await aiService.getResponse(for: transcription)
                addLog("AI response: \(responseText)", icon: "sparkles", color: Color(hex: "CCA427"))
            }

            // Add assistant message to chat
            messages.append(ChatMessage(role: .assistant, text: responseText))

            // Send response back to watch
            currentStatus = "Sending to watch..."
            bleManager.writeResponse(responseText)
            addLog("Sent to watch", icon: "checkmark.circle.fill", color: .green)

        } catch {
            addLog("Error: \(error.localizedDescription)", icon: "xmark.circle", color: .red)
        }

        resetState()
    }

    private func resetState() {
        audioDecoder.reset()
        currentStatus = nil
        isProcessing = false
    }

    private func addLog(_ message: String, icon: String = "circle.fill", color: Color = .gray) {
        activityLog.append(ActivityEntry(message: message, icon: icon, color: color))

        // Keep log manageable
        if activityLog.count > 100 {
            activityLog.removeFirst(activityLog.count - 100)
        }
    }
}
