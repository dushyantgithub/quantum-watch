import Foundation
import Speech
import AVFoundation

protocol SpeechService {
    func transcribe(pcmData: Data, sampleRate: Double) async throws -> String
}

// MARK: - WAV Helper

func createWAVData(from pcmData: Data, sampleRate: Double) -> Data {
    let numChannels: UInt16 = 1
    let bitsPerSample: UInt16 = 16
    let byteRate = UInt32(sampleRate) * UInt32(numChannels) * UInt32(bitsPerSample / 8)
    let blockAlign = numChannels * (bitsPerSample / 8)
    let dataSize = UInt32(pcmData.count)
    let chunkSize = 36 + dataSize

    var header = Data()
    header.append(contentsOf: "RIFF".utf8)
    header.append(withUnsafeBytes(of: chunkSize.littleEndian) { Data($0) })
    header.append(contentsOf: "WAVE".utf8)
    header.append(contentsOf: "fmt ".utf8)
    header.append(withUnsafeBytes(of: UInt32(16).littleEndian) { Data($0) })
    header.append(withUnsafeBytes(of: UInt16(1).littleEndian) { Data($0) })
    header.append(withUnsafeBytes(of: numChannels.littleEndian) { Data($0) })
    header.append(withUnsafeBytes(of: UInt32(sampleRate).littleEndian) { Data($0) })
    header.append(withUnsafeBytes(of: byteRate.littleEndian) { Data($0) })
    header.append(withUnsafeBytes(of: blockAlign.littleEndian) { Data($0) })
    header.append(withUnsafeBytes(of: bitsPerSample.littleEndian) { Data($0) })
    header.append(contentsOf: "data".utf8)
    header.append(withUnsafeBytes(of: dataSize.littleEndian) { Data($0) })
    header.append(pcmData)

    return header
}

// MARK: - Apple Speech

class AppleSpeechService: SpeechService {
    func transcribe(pcmData: Data, sampleRate: Double) async throws -> String {
        let status = await withCheckedContinuation { continuation in
            SFSpeechRecognizer.requestAuthorization { status in
                continuation.resume(returning: status)
            }
        }

        guard status == .authorized else {
            throw SpeechError.notAuthorized
        }

        guard let recognizer = SFSpeechRecognizer(), recognizer.isAvailable else {
            throw SpeechError.notAvailable
        }

        let tempURL = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString)
            .appendingPathExtension("wav")

        let wavData = createWAVData(from: pcmData, sampleRate: sampleRate)
        try wavData.write(to: tempURL)
        defer { try? FileManager.default.removeItem(at: tempURL) }

        let request = SFSpeechURLRecognitionRequest(url: tempURL)
        request.shouldReportPartialResults = false

        return try await withCheckedThrowingContinuation { continuation in
            recognizer.recognitionTask(with: request) { result, error in
                if let error = error {
                    continuation.resume(throwing: error)
                } else if let result = result, result.isFinal {
                    continuation.resume(returning: result.bestTranscription.formattedString)
                }
            }
        }
    }
}

// MARK: - Whisper API

class WhisperService: SpeechService {
    private let apiKey: String

    init(apiKey: String) {
        self.apiKey = apiKey
    }

    func transcribe(pcmData: Data, sampleRate: Double) async throws -> String {
        let wavData = createWAVData(from: pcmData, sampleRate: sampleRate)

        let boundary = UUID().uuidString
        var request = URLRequest(url: URL(string: "https://api.openai.com/v1/audio/transcriptions")!)
        request.httpMethod = "POST"
        request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        request.setValue("multipart/form-data; boundary=\(boundary)", forHTTPHeaderField: "Content-Type")

        var body = Data()
        body.append("--\(boundary)\r\n".data(using: .utf8)!)
        body.append("Content-Disposition: form-data; name=\"model\"\r\n\r\n".data(using: .utf8)!)
        body.append("whisper-1\r\n".data(using: .utf8)!)
        body.append("--\(boundary)\r\n".data(using: .utf8)!)
        body.append("Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n".data(using: .utf8)!)
        body.append("Content-Type: audio/wav\r\n\r\n".data(using: .utf8)!)
        body.append(wavData)
        body.append("\r\n--\(boundary)--\r\n".data(using: .utf8)!)

        request.httpBody = body

        let (data, response) = try await URLSession.shared.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse, httpResponse.statusCode == 200 else {
            throw SpeechError.apiError("Whisper API returned error")
        }

        let result = try JSONDecoder().decode(WhisperResponse.self, from: data)
        return result.text
    }
}

private struct WhisperResponse: Decodable {
    let text: String
}

enum SpeechError: LocalizedError {
    case notAuthorized
    case notAvailable
    case apiError(String)

    var errorDescription: String? {
        switch self {
        case .notAuthorized: return "Speech recognition not authorized"
        case .notAvailable: return "Speech recognition not available"
        case .apiError(let msg): return msg
        }
    }
}
