import Foundation

class AIService {
    private let apiKey: String
    private let model: String

    init(apiKey: String, model: String = "gpt-4o-mini") {
        self.apiKey = apiKey
        self.model = model
    }

    func getResponse(for text: String) async throws -> String {
        guard !apiKey.isEmpty else {
            return text // No API key, return transcription as-is
        }

        var request = URLRequest(url: URL(string: "https://api.openai.com/v1/chat/completions")!)
        request.httpMethod = "POST"
        request.setValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")

        let body: [String: Any] = [
            "model": model,
            "messages": [
                [
                    "role": "system",
                    "content": "You are a helpful smartwatch assistant. Keep responses concise (under 100 words) since they display on a small watch screen."
                ],
                [
                    "role": "user",
                    "content": text
                ]
            ],
            "max_tokens": 200,
            "temperature": 0.7
        ]

        request.httpBody = try JSONSerialization.data(withJSONObject: body)

        let (data, response) = try await URLSession.shared.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse, httpResponse.statusCode == 200 else {
            let errorBody = String(data: data, encoding: .utf8) ?? "Unknown error"
            throw AIError.apiError("OpenAI API error: \(errorBody)")
        }

        let result = try JSONDecoder().decode(ChatResponse.self, from: data)
        return result.choices.first?.message.content ?? text
    }
}

private struct ChatResponse: Decodable {
    let choices: [Choice]

    struct Choice: Decodable {
        let message: Message
    }

    struct Message: Decodable {
        let content: String
    }
}

enum AIError: LocalizedError {
    case apiError(String)

    var errorDescription: String? {
        switch self {
        case .apiError(let msg): return msg
        }
    }
}
