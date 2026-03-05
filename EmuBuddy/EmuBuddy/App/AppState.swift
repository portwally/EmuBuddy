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
    /// Persists the selected machine profile across tab switches.
    @Published var selectedMachineProfileID: UUID?

    // MARK: - Services
    let mameEngine: any MAMEEngine
    let libraryService: LibraryService
    let configStore: ConfigStore
    let saveStateManager: SaveStateManager

    /// Status bar menu (system tray) for emulation controls — visible even when MAME is frontmost.
    private(set) var emulationStatusBar: EmulationStatusBar?

    /// Forwards nested ObservableObject changes so SwiftUI redraws properly.
    private var cancellables = Set<AnyCancellable>()

    init() {
        self.configStore = ConfigStore()
        self.mameEngine = SubprocessMAMEEngine(config: configStore)
        self.libraryService = LibraryService(configStore: configStore)
        self.saveStateManager = SaveStateManager(configStore: configStore)

        // Check if MAME is configured on launch
        self.isMAMEConfigured = configStore.mameBinaryURL != nil

        // Forward libraryService changes so views observing AppState
        // redraw when library items/scanning state changes.
        libraryService.objectWillChange
            .sink { [weak self] _ in
                self?.objectWillChange.send()
            }
            .store(in: &cancellables)

        // Set up status bar emulation controls (appears when MAME is running)
        self.emulationStatusBar = EmulationStatusBar(appState: self)
    }

    // MARK: - Launch

    /// Launch a MAME emulation session with the given profile and media.
    func launchSession(profile: MachineProfile, media: [MediaSlot: URL]) async {
        print("[EmuBuddy] launchSession called — profile: \(profile.name), media: \(media.map { "\($0.key.displayName): \($0.value.lastPathComponent)" })")

        guard let config = configStore.mameConfig() else {
            print("[EmuBuddy] ERROR: mameConfig() returned nil")
            print("[EmuBuddy]   mameBinaryURL: \(String(describing: configStore.mameBinaryURL))")
            print("[EmuBuddy]   romDirectoryURL: \(String(describing: configStore.romDirectoryURL))")
            launchError = "MAME is not configured. Please run the setup wizard."
            return
        }

        print("[EmuBuddy] Config OK — binary: \(config.mameBinaryURL.path), roms: \(config.romPath.path)")
        launchError = nil

        // Ensure all MAME support directories exist
        let fm = FileManager.default
        for dir in [config.cfgPath, config.nvramPath, config.statePath, config.snapshotPath] {
            try? fm.createDirectory(at: dir, withIntermediateDirectories: true)
        }

        // Verify media files exist
        for (slot, url) in media {
            guard fm.fileExists(atPath: url.path) else {
                print("[EmuBuddy] ERROR: Media file not found — \(slot.displayName): \(url.path)")
                launchError = "Media file not found for \(slot.displayName): \(url.lastPathComponent)"
                return
            }
        }

        print("[EmuBuddy] Media files verified, launching...")

        // Remember which profile was used
        configStore.lastUsedProfileID = profile.id

        do {
            let session = try await mameEngine.launch(
                machine: profile,
                media: media,
                config: config
            )
            print("[EmuBuddy] Launch succeeded, PID: \(session.processID)")
            activeSession = session

            // Record play in library for recently-played tracking
            for (_, url) in media {
                libraryService.recordPlay(for: url)
            }

            // Monitor for termination
            Task {
                print("[EmuBuddy] Starting status stream monitor...")
                for await status in mameEngine.statusStream {
                    switch status {
                    case .terminated(let exitCode):
                        print("[EmuBuddy] MAME terminated with exit code: \(exitCode)")
                        await MainActor.run {
                            if exitCode != 0 {
                                launchError = "MAME exited unexpectedly (code \(exitCode))."
                            }
                            activeSession = nil
                        }
                    case .error(let msg):
                        print("[EmuBuddy] MAME error: \(msg)")
                        await MainActor.run {
                            launchError = msg
                            activeSession = nil
                        }
                    default:
                        print("[EmuBuddy] Status: \(status)")
                    }
                }
                print("[EmuBuddy] Status stream ended")
            }
        } catch {
            print("[EmuBuddy] Launch failed: \(error)")
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
