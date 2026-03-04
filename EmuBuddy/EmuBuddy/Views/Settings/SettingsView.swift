import SwiftUI

/// App preferences: MAME path, ROM directory, disk image folders, display defaults.
struct SettingsView: View {
    @EnvironmentObject var appState: AppState

    var body: some View {
        TabView {
            GeneralSettingsView()
                .tabItem {
                    Label("General", systemImage: "gear")
                }

            DisplaySettingsView()
                .tabItem {
                    Label("Display", systemImage: "display")
                }

            InputSettingsView()
                .tabItem {
                    Label("Input", systemImage: "keyboard")
                }
        }
        .frame(width: 500, height: 400)
    }
}

// MARK: - General Settings

struct GeneralSettingsView: View {
    @EnvironmentObject var appState: AppState
    @State private var mamePath: String = ""
    @State private var romPath: String = ""

    var body: some View {
        Form {
            Section("MAME") {
                HStack {
                    TextField("MAME Binary", text: $mamePath)
                        .textFieldStyle(.roundedBorder)
                    Button("Browse...") {
                        // TODO: File picker for MAME binary
                    }
                }
            }

            Section("ROMs") {
                HStack {
                    TextField("ROM Directory", text: $romPath)
                        .textFieldStyle(.roundedBorder)
                    Button("Browse...") {
                        // TODO: Folder picker for ROMs
                    }
                }
                Button("Validate ROMs...") {
                    // TODO: Run ROM validation for all machine types
                }
            }

            Section("Disk Image Folders") {
                // TODO: List of watched folders with add/remove
                Text("Configure folders to scan for disk images.")
                    .foregroundStyle(.secondary)
                Button("Add Folder...") {
                    // TODO: Folder picker
                }
            }
        }
        .padding()
    }
}

// MARK: - Display Settings (Placeholder)

struct DisplaySettingsView: View {
    var body: some View {
        Form {
            Section("Default Display") {
                Text("Display settings will appear here.")
                    .foregroundStyle(.secondary)
            }
        }
        .padding()
    }
}

// MARK: - Input Settings (Placeholder)

struct InputSettingsView: View {
    var body: some View {
        Form {
            Section("Keyboard") {
                Text("Input mapping settings will appear here.")
                    .foregroundStyle(.secondary)
            }
        }
        .padding()
    }
}
