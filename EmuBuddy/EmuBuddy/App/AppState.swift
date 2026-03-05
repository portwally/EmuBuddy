import SwiftUI
import Combine

/// Central app state coordinating services and active sessions.
@MainActor
final class AppState: ObservableObject {

    // MARK: - Published State
    @Published var selectedSidebarItem: SidebarItem = .library
    @Published var activeSession: EmulationSession?
    @Published var isMAMEConfigured: Bool = false
    @Published var launchError: String?

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

    // MARK: - Launch

    /// Launch a MAME emulation session with the given profile and media.
    func launchSession(profile: MachineProfile, media: [MediaSlot: URL]) async {
        guard let config = configStore.mameConfig() else {
            launchError = "MAME is not configured. Please run the setup wizard."
            return
        }

        launchError = nil

        do {
            let session = try await mameEngine.launch(
                machine: profile,
                media: media,
                config: config
            )
            activeSession = session

            // Monitor for termination
            Task {
                for await status in mameEngine.statusStream {
                    switch status {
                    case .terminated:
                        activeSession = nil
                    case .error(let msg):
                        launchError = msg
                        activeSession = nil
                    default:
                        break
                    }
                }
            }
        } catch {
            launchError = error.localizedDescription
        }
    }

    /// Stop the current emulation session.
    func stopSession() async {
        guard let session = activeSession else { return }
        await mameEngine.terminate(session: session)
        activeSession = nil
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
