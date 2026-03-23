import SwiftUI

// MARK: - Theme Manager

enum AppTheme: String, CaseIterable {
    case dark
    case light
    case system

    var displayName: String {
        switch self {
        case .dark: return "Dark"
        case .light: return "Light"
        case .system: return "System"
        }
    }

    var colorScheme: ColorScheme? {
        switch self {
        case .dark: return .dark
        case .light: return .light
        case .system: return nil
        }
    }
}

class ThemeManager: ObservableObject {
    @Published var currentTheme: AppTheme {
        didSet {
            UserDefaults.standard.set(currentTheme.rawValue, forKey: "app_theme")
        }
    }

    init() {
        let saved = UserDefaults.standard.string(forKey: "app_theme") ?? "dark"
        self.currentTheme = AppTheme(rawValue: saved) ?? .dark
    }
}

// MARK: - Design System Colors
// Derived from the QuantumWatch Design_System.png

extension Color {
    struct QW {
        // Neutrals (dark palette)
        static let neutral100 = Color(hex: "141420")
        static let neutral150 = Color(hex: "1A1A2E")
        static let neutral200 = Color(hex: "22223A")
        static let neutral250 = Color(hex: "2A2A44")
        static let neutral300 = Color(hex: "33334D")
        static let neutral350 = Color(hex: "3D3D57")
        static let neutral400 = Color(hex: "484862")
        static let neutral450 = Color(hex: "54546D")
        static let neutral500 = Color(hex: "616178")

        // Blue-gray (secondary)
        static let blueGray550 = Color(hex: "6B7B8D")
        static let blueGray600 = Color(hex: "7D8D9E")
        static let blueGray650 = Color(hex: "8FA0AF")
        static let blueGray700 = Color(hex: "A1B2C0")
        static let blueGray750 = Color(hex: "B3C3D1")
        static let blueGray800 = Color(hex: "C5D4E2")
        static let blueGray850 = Color(hex: "D7E5F0")
        static let blueGray900 = Color(hex: "E9F0F7")

        // Blue accent
        static let blue100 = Color(hex: "1E3A5F")
        static let blue200 = Color(hex: "2563EB")
        static let blue300 = Color(hex: "3B82F6")
        static let blue400 = Color(hex: "60A5FA")
        static let blue500 = Color(hex: "5BA3F5")
        static let blue600 = Color(hex: "4B8FE0")
        static let blue700 = Color(hex: "3A7BC8")

        // Green
        static let green200 = Color(hex: "22C55E")
        static let green400 = Color(hex: "4ADE80")
        static let green600 = Color(hex: "16A34A")

        // Gold / Accent
        static let gold200 = Color(hex: "F5D060")
        static let gold400 = Color(hex: "CCA427")
        static let gold600 = Color(hex: "A67C00")

        // Red / Error
        static let red200 = Color(hex: "F87171")
        static let red400 = Color(hex: "EF4444")
        static let red600 = Color(hex: "DC2626")
    }
}

// MARK: - Semantic Theme Colors

struct ThemeColors {
    let background: Color
    let surface: Color
    let surfaceElevated: Color
    let textPrimary: Color
    let textSecondary: Color
    let textTertiary: Color
    let accent: Color
    let accentSubtle: Color
    let separator: Color
    let cardBackground: Color
    let listRowBackground: Color
}

extension ThemeColors {
    static let dark = ThemeColors(
        background: Color.QW.neutral100,
        surface: Color.QW.neutral150,
        surfaceElevated: Color.QW.neutral200,
        textPrimary: .white,
        textSecondary: Color.QW.blueGray700,
        textTertiary: Color.QW.blueGray550,
        accent: Color.QW.gold400,
        accentSubtle: Color.QW.gold400.opacity(0.25),
        separator: Color.QW.neutral300,
        cardBackground: Color.QW.neutral150,
        listRowBackground: Color.QW.neutral100
    )

    static let light = ThemeColors(
        background: Color.QW.blueGray900,
        surface: .white,
        surfaceElevated: Color(hex: "F0F4F8"),
        textPrimary: Color.QW.neutral100,
        textSecondary: Color.QW.neutral400,
        textTertiary: Color.QW.neutral500,
        accent: Color.QW.gold600,
        accentSubtle: Color.QW.gold400.opacity(0.15),
        separator: Color.QW.blueGray800,
        cardBackground: .white,
        listRowBackground: Color.QW.blueGray900
    )

    static func forScheme(_ scheme: ColorScheme) -> ThemeColors {
        scheme == .dark ? .dark : .light
    }
}

// MARK: - Environment Key

private struct ThemeColorsKey: EnvironmentKey {
    static let defaultValue: ThemeColors = .dark
}

extension EnvironmentValues {
    var themeColors: ThemeColors {
        get { self[ThemeColorsKey.self] }
        set { self[ThemeColorsKey.self] = newValue }
    }
}

// MARK: - Design System Typography

extension Font {
    struct QW {
        static let heading1 = Font.system(size: 28, weight: .bold, design: .default)
        static let heading2 = Font.system(size: 22, weight: .bold, design: .default)
        static let heading3 = Font.system(size: 17, weight: .semibold, design: .default)
        static let overline = Font.system(size: 11, weight: .medium, design: .default)
        static let label1 = Font.system(size: 15, weight: .regular, design: .default)
        static let label2 = Font.system(size: 12, weight: .medium, design: .default)
        static let body = Font.system(size: 14, weight: .regular, design: .default)
        static let caption = Font.system(size: 11, weight: .regular, design: .default)
    }
}
