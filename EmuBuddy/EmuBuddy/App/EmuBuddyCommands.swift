import SwiftUI

// MARK: - Focused Values

/// Pass the active AppState down through the focused scene for menu commands.
struct FocusedAppStateKey: FocusedValueKey {
    typealias Value = AppState
}

extension FocusedValues {
    var appState: AppState? {
        get { self[FocusedAppStateKey.self] }
        set { self[FocusedAppStateKey.self] = newValue }
    }
}

/// Custom menu bar commands for EmuBuddy.
struct EmuBuddyCommands: Commands {
    @FocusedValue(\.appState) var appState

    private var hasSession: Bool {
        appState?.activeSession != nil
    }

    var body: some Commands {
        // Emulation menu
        CommandMenu("Emulation") {
            Button("Save State") {
                // MAME handles save states via its own key bindings (Shift+F7)
                // In Phase 2 (libMAME), we'll intercept this directly
                NotificationCenter.default.post(name: .emubuddySaveState, object: nil)
            }
            .keyboardShortcut("s", modifiers: [.command, .shift])
            .disabled(!hasSession)

            Button("Load State...") {
                NotificationCenter.default.post(name: .emubuddyLoadState, object: nil)
            }
            .keyboardShortcut("l", modifiers: [.command, .shift])
            .disabled(!hasSession)

            Divider()

            Button("Swap Disk 1...") {
                NotificationCenter.default.post(name: .emubuddySwapDisk, object: 1)
            }
            .keyboardShortcut("1", modifiers: [.command, .option])
            .disabled(!hasSession)

            Button("Swap Disk 2...") {
                NotificationCenter.default.post(name: .emubuddySwapDisk, object: 2)
            }
            .keyboardShortcut("2", modifiers: [.command, .option])
            .disabled(!hasSession)

            Divider()

            Button("Reset Machine") {
                Task { @MainActor in
                    guard let session = appState?.activeSession else { return }
                    await appState?.mameEngine.sendInput(session: session, input: .reset)
                }
            }
            .keyboardShortcut("r", modifiers: [.command, .shift])
            .disabled(!hasSession)

            Button("Stop Emulation") {
                Task { @MainActor in
                    await appState?.stopSession()
                }
            }
            .keyboardShortcut("q", modifiers: [.command, .shift])
            .disabled(!hasSession)
        }

        // File menu additions
        CommandGroup(after: .newItem) {
            Button("Open Disk Image...") {
                NotificationCenter.default.post(name: .emubuddyOpenDiskImage, object: nil)
            }
            .keyboardShortcut("o", modifiers: [.command])
        }
    }
}

// MARK: - Notification Names

extension Notification.Name {
    static let emubuddySaveState = Notification.Name("emubuddySaveState")
    static let emubuddyLoadState = Notification.Name("emubuddyLoadState")
    static let emubuddySwapDisk = Notification.Name("emubuddySwapDisk")
    static let emubuddyOpenDiskImage = Notification.Name("emubuddyOpenDiskImage")
}
