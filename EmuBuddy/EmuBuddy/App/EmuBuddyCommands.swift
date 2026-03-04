import SwiftUI

/// Custom menu bar commands for EmuBuddy.
struct EmuBuddyCommands: Commands {
    var body: some Commands {
        // Emulation menu
        CommandMenu("Emulation") {
            Button("Save State") {
                // TODO: Save state for active session
            }
            .keyboardShortcut("s", modifiers: [.command, .shift])

            Button("Load State...") {
                // TODO: Load state picker
            }
            .keyboardShortcut("l", modifiers: [.command, .shift])

            Divider()

            Button("Swap Disk 1...") {
                // TODO: Open disk picker for slot 1
            }
            .keyboardShortcut("1", modifiers: [.command, .option])

            Button("Swap Disk 2...") {
                // TODO: Open disk picker for slot 2
            }
            .keyboardShortcut("2", modifiers: [.command, .option])

            Divider()

            Button("Reset Machine") {
                // TODO: Send reset to MAME
            }
            .keyboardShortcut("r", modifiers: [.command, .shift])
        }
    }
}
