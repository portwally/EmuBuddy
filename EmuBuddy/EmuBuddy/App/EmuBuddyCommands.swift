import SwiftUI

// MARK: - Focused Values

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
/// These appear in EmuBuddy's own menu bar (visible when EmuBuddy is the active app).
/// Controls send Lua commands to MAME via file-based IPC plugin.
struct EmuBuddyCommands: Commands {
    @FocusedValue(\.appState) var appState

    private var hasSession: Bool {
        appState?.activeSession != nil
    }

    var body: some Commands {
        // Emulation menu
        CommandMenu("Emulation") {
            Button("Pause / Resume") { MAMELuaCommand.togglePause() }
                .keyboardShortcut("p", modifiers: [.command, .option])
                .disabled(!hasSession)

            Button("Frame Advance") { MAMELuaCommand.frameAdvance() }
                .keyboardShortcut(".", modifiers: [.command, .option])
                .disabled(!hasSession)

            Button("Toggle Throttle") { MAMELuaCommand.toggleThrottle() }
                .keyboardShortcut("t", modifiers: [.command, .option])
                .disabled(!hasSession)

            Divider()

            Button("Soft Reset") { MAMELuaCommand.softReset() }
                .keyboardShortcut("r", modifiers: [.command, .option])
                .disabled(!hasSession)

            Button("Hard Reset") { MAMELuaCommand.hardReset() }
                .keyboardShortcut("r", modifiers: [.command, .option, .shift])
                .disabled(!hasSession)

            Divider()

            Button("Quick Save") { MAMELuaCommand.saveState() }
                .keyboardShortcut("s", modifiers: [.command, .option])
                .disabled(!hasSession)

            Button("Quick Load") { MAMELuaCommand.loadState() }
                .keyboardShortcut("l", modifiers: [.command, .option])
                .disabled(!hasSession)

            Divider()

            Button("Screenshot") { MAMELuaCommand.screenshot() }
                .keyboardShortcut("c", modifiers: [.command, .option])
                .disabled(!hasSession)

            Divider()

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
    static let emubuddyOpenDiskImage = Notification.Name("emubuddyOpenDiskImage")
}
