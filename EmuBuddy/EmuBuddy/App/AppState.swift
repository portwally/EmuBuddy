import SwiftUI
import Combine

/// Central app state coordinating services and active sessions.
@MainActor
final class AppState: ObservableObject {

    // MARK: - Published State
    @Published var selectedSidebarItem: SidebarItem = .library
    @Published var activeSession: EmulationSession?
    @Published var isMAMEConfigured: Bool = false

    // MARK: - Services
    let mameEngine: any MAMEEngine
    let libraryService: LibraryService
    let configStore: ConfigStore
    let saveStateManager: SaveStateManager

    init() {
        self.configStore = ConfigStore()
        self.mameEngine = SubprocessMAMEEngine(config: configStore)
        self.libraryService = LibraryService(configStore: configStore)
        self.saveStateManager = SaveStateManager(configStore: configStore)

        // Check if MAME is configured on launch
        self.isMAMEConfigured = configStore.mameBinaryURL != nil
    }
}

enum SidebarItem: String, CaseIterable, Identifiable {
    case library = "Library"
    case machines = "Machines"
    case recentlyPlayed = "Recently Played"
    case favorites = "Favorites"
    case settings = "Settings"

    var id: String { rawValue }

    var systemImage: String {
        switch self {
        case .library: return "square.grid.2x2"
        case .machines: return "desktopcomputer"
        case .recentlyPlayed: return "clock"
        case .favorites: return "star"
        case .settings: return "gear"
        }
    }
}
