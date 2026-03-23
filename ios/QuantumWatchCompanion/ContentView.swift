import SwiftUI

struct ContentView: View {
    @EnvironmentObject var viewModel: VoiceAssistantViewModel
    @EnvironmentObject var themeManager: ThemeManager
    @Environment(\.colorScheme) var colorScheme
    @State private var showSettings = false

    private var theme: ThemeColors {
        ThemeColors.forScheme(colorScheme)
    }

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                if viewModel.messages.isEmpty {
                    emptyState
                } else {
                    chatList
                }

                if viewModel.isProcessing {
                    processingBar
                }
            }
            .background(theme.background)
            .navigationTitle("QuantumWatch")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button {
                        showSettings = true
                    } label: {
                        Image(systemName: "gearshape.fill")
                            .foregroundColor(theme.accent)
                    }
                }
            }
            .sheet(isPresented: $showSettings) {
                SettingsView()
                    .environmentObject(viewModel)
                    .environmentObject(themeManager)
            }
        }
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Spacer()
            Image(systemName: "waveform.circle")
                .font(.system(size: 48))
                .foregroundColor(theme.textTertiary.opacity(0.5))
            Text("No conversations yet")
                .font(.QW.label1)
                .foregroundColor(theme.textTertiary)
            Text("Speak into your watch to start")
                .font(.QW.caption)
                .foregroundColor(theme.textTertiary.opacity(0.7))
            Spacer()
        }
        .frame(maxWidth: .infinity)
    }

    private var chatList: some View {
        ScrollViewReader { proxy in
            ScrollView {
                LazyVStack(spacing: 4) {
                    ForEach(viewModel.messages) { message in
                        ChatBubbleView(message: message, theme: theme)
                            .id(message.id)
                    }
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
            }
            .onChange(of: viewModel.messages.count) { _ in
                if let last = viewModel.messages.last {
                    withAnimation(.easeOut(duration: 0.2)) {
                        proxy.scrollTo(last.id, anchor: .bottom)
                    }
                }
            }
        }
    }

    private var processingBar: some View {
        HStack(spacing: 8) {
            ProgressView()
                .tint(theme.accent)
                .scaleEffect(0.8)
            Text(viewModel.currentStatus ?? "Processing...")
                .font(.QW.caption)
                .foregroundColor(theme.textSecondary)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 8)
        .background(theme.surface)
    }
}

// MARK: - Chat Bubble

struct ChatBubbleView: View {
    let message: ChatMessage
    let theme: ThemeColors

    private var isUser: Bool { message.role == .user }

    var body: some View {
        HStack {
            if isUser { Spacer(minLength: 60) }

            VStack(alignment: isUser ? .trailing : .leading, spacing: 4) {
                Text(message.text)
                    .font(.QW.body)
                    .foregroundColor(isUser ? .white : theme.textPrimary)
                    .padding(.horizontal, 14)
                    .padding(.vertical, 10)
                    .background(bubbleBackground)
                    .clipShape(BubbleShape(isUser: isUser))

                Text(message.timestamp, style: .time)
                    .font(.QW.caption)
                    .foregroundColor(theme.textTertiary)
                    .padding(.horizontal, 4)
            }

            if !isUser { Spacer(minLength: 60) }
        }
        .padding(.vertical, 2)
    }

    private var bubbleBackground: Color {
        isUser ? Color.QW.gold400 : theme.surfaceElevated
    }
}

// MARK: - Bubble Shape (WhatsApp-style tail)

struct BubbleShape: Shape {
    let isUser: Bool

    func path(in rect: CGRect) -> Path {
        let radius: CGFloat = 16
        let tailSize: CGFloat = 6

        var path = Path()

        if isUser {
            // User bubble: rounded with tail on bottom-right
            path.move(to: CGPoint(x: rect.minX + radius, y: rect.minY))
            path.addLine(to: CGPoint(x: rect.maxX - radius, y: rect.minY))
            path.addQuadCurve(
                to: CGPoint(x: rect.maxX, y: rect.minY + radius),
                control: CGPoint(x: rect.maxX, y: rect.minY)
            )
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - radius))
            path.addQuadCurve(
                to: CGPoint(x: rect.maxX - radius, y: rect.maxY),
                control: CGPoint(x: rect.maxX, y: rect.maxY)
            )
            // Tail
            path.addLine(to: CGPoint(x: rect.maxX - radius + tailSize, y: rect.maxY))
            path.addQuadCurve(
                to: CGPoint(x: rect.maxX - radius, y: rect.maxY + tailSize),
                control: CGPoint(x: rect.maxX - radius, y: rect.maxY)
            )
            path.addQuadCurve(
                to: CGPoint(x: rect.maxX - radius - tailSize, y: rect.maxY),
                control: CGPoint(x: rect.maxX - radius - tailSize / 2, y: rect.maxY)
            )
            path.addLine(to: CGPoint(x: rect.minX + radius, y: rect.maxY))
            path.addQuadCurve(
                to: CGPoint(x: rect.minX, y: rect.maxY - radius),
                control: CGPoint(x: rect.minX, y: rect.maxY)
            )
            path.addLine(to: CGPoint(x: rect.minX, y: rect.minY + radius))
            path.addQuadCurve(
                to: CGPoint(x: rect.minX + radius, y: rect.minY),
                control: CGPoint(x: rect.minX, y: rect.minY)
            )
        } else {
            // Assistant bubble: rounded with tail on bottom-left
            path.move(to: CGPoint(x: rect.minX + radius, y: rect.minY))
            path.addLine(to: CGPoint(x: rect.maxX - radius, y: rect.minY))
            path.addQuadCurve(
                to: CGPoint(x: rect.maxX, y: rect.minY + radius),
                control: CGPoint(x: rect.maxX, y: rect.minY)
            )
            path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - radius))
            path.addQuadCurve(
                to: CGPoint(x: rect.maxX - radius, y: rect.maxY),
                control: CGPoint(x: rect.maxX, y: rect.maxY)
            )
            path.addLine(to: CGPoint(x: rect.minX + radius + tailSize, y: rect.maxY))
            // Tail
            path.addQuadCurve(
                to: CGPoint(x: rect.minX + radius, y: rect.maxY + tailSize),
                control: CGPoint(x: rect.minX + radius + tailSize / 2, y: rect.maxY)
            )
            path.addQuadCurve(
                to: CGPoint(x: rect.minX + radius - tailSize, y: rect.maxY),
                control: CGPoint(x: rect.minX + radius, y: rect.maxY)
            )
            path.addLine(to: CGPoint(x: rect.minX + radius, y: rect.maxY))
            path.addQuadCurve(
                to: CGPoint(x: rect.minX, y: rect.maxY - radius),
                control: CGPoint(x: rect.minX, y: rect.maxY)
            )
            path.addLine(to: CGPoint(x: rect.minX, y: rect.minY + radius))
            path.addQuadCurve(
                to: CGPoint(x: rect.minX + radius, y: rect.minY),
                control: CGPoint(x: rect.minX, y: rect.minY)
            )
        }

        path.closeSubpath()
        return path
    }
}

extension Color {
    init(hex: String) {
        let scanner = Scanner(string: hex)
        var rgbValue: UInt64 = 0
        scanner.scanHexInt64(&rgbValue)
        self.init(
            red: Double((rgbValue & 0xFF0000) >> 16) / 255.0,
            green: Double((rgbValue & 0x00FF00) >> 8) / 255.0,
            blue: Double(rgbValue & 0x0000FF) / 255.0
        )
    }
}

#Preview {
    ContentView()
        .environmentObject(VoiceAssistantViewModel())
        .environmentObject(ThemeManager())
}
