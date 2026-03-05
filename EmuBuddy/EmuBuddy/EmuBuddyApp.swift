import SwiftUI

@main
struct EmuBuddyApp: App {
    @StateObject private var appState = AppState()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(appState)
                .focusedValue(\.appState, appState)
        }
        .commands {
            EmuBuddyCommands()
        }

        Settings {
            SettingsView()
                .environmentObject(appState)
        }
    }
}
