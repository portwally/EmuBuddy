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
                .environmentObject(appState)

            DisplaySettingsView()
                .tabItem {
                    Label("Display", systemImage: "display")
                }

            InputSettingsView()
                .tabItem {
                    Label("Input", systemImage: "keyboard")
                }
        }
        .frame(width: 550, height: 450)
    }
}

// MARK: - General Settings

struct GeneralSettingsView: View {
    @EnvironmentObject var appState: AppState
    @State private var mamePath: String = ""
    @State private var romPath: String = ""
    @State private var diskImageFolders: [URL] = []
    @State private var romValidation: [MachineType: Bool] = [:]
    @State private var isValidating = false

    var body: some View {
        Form {
            Section("MAME Binary") {
                HStack {
                    TextField("Path to emubuddy binary", text: $mamePath)
                        .textFieldStyle(.roundedBorder)
                    Button("Browse...") {
                        browseForMAME()
                    }
                }
                if !mamePath.isEmpty {
                    if isBinaryValid(mamePath) {
                        Label("Binary found", systemImage: "checkmark.circle.fill")
                            .foregroundStyle(.green)
                            .font(.caption)
                    } else {
                        Label("File not found at this path", systemImage: "xmark.circle.fill")
                            .foregroundStyle(.red)
                            .font(.caption)
                    }
                }
            }

            Section("ROM Directory") {
                HStack {
                    TextField("Path to ROM directory", text: $romPath)
                        .textFieldStyle(.roundedBorder)
                    Button("Browse...") {
                        browseForROMs()
                    }
                }

                if !romPath.isEmpty {
                    Button("Validate ROMs") {
                        validateROMs()
                    }
                    .disabled(isValidating)

                    if !romValidation.isEmpty {
                        ForEach(Array(romValidation.keys).sorted(by: { $0.displayName < $1.displayName }), id: \.self) { machine in
                            HStack {
                                Image(systemName: romValidation[machine] == true ? "checkmark.circle.fill" : "xmark.circle")
                                    .foregroundStyle(romValidation[machine] == true ? .green : .orange)
                                    .font(.caption)
                                Text(machine.displayName)
                                    .font(.caption)
                            }
                        }
                    }
                }
            }

            Section("Disk Image Folders") {
                if diskImageFolders.isEmpty {
                    Text("No folders configured. Add folders containing your Apple II disk images.")
                        .foregroundStyle(.secondary)
                        .font(.callout)
                } else {
                    ForEach(diskImageFolders, id: \.self) { url in
                        HStack {
                            Image(systemName: "folder.fill")
                                .foregroundStyle(.tint)
                            Text(url.path)
                                .lineLimit(1)
                                .truncationMode(.middle)
                                .font(.callout)
                            Spacer()
                            Button(action: {
                                diskImageFolders.removeAll { $0 == url }
                                saveFolders()
                            }) {
                                Image(systemName: "minus.circle.fill")
                                    .foregroundStyle(.red)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                }

                Button("Add Folder...") {
                    browseForDiskImageFolder()
                }
            }
        }
        .padding()
        .onAppear {
            mamePath = appState.configStore.mameBinaryURL?.path ?? ""
            romPath = appState.configStore.romDirectoryURL?.path ?? ""
            diskImageFolders = appState.configStore.diskImageDirectories
        }
    }

    private func browseForMAME() {
        let panel = NSOpenPanel()
        panel.title = "Select MAME Binary"
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            mamePath = url.path
            appState.configStore.mameBinaryURL = url
            appState.isMAMEConfigured = true
        }
    }

    private func browseForROMs() {
        let panel = NSOpenPanel()
        panel.title = "Select ROM Directory"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            romPath = url.path
            appState.configStore.romDirectoryURL = url
        }
    }

    private func browseForDiskImageFolder() {
        let panel = NSOpenPanel()
        panel.title = "Select Disk Image Folder"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        if panel.runModal() == .OK {
            for url in panel.urls where !diskImageFolders.contains(url) {
                diskImageFolders.append(url)
            }
            saveFolders()
        }
    }

    private func saveFolders() {
        appState.configStore.diskImageDirectories = diskImageFolders
        // Trigger a library rescan whenever folders change
        Task {
            await appState.libraryService.scanAll()
        }
    }

    /// Check if a binary exists. Under App Sandbox, isExecutableFile can return false
    /// even for valid executables, so we fall back to just checking existence.
    private func isBinaryValid(_ path: String) -> Bool {
        let fm = FileManager.default
        if fm.isExecutableFile(atPath: path) { return true }
        // Fallback: check POSIX permissions or trust that the file exists
        return fm.fileExists(atPath: path)
    }

    private func validateROMs() {
        isValidating = true
        let romURL = URL(fileURLWithPath: romPath)
        let machinesToCheck: [MachineType] = [
            .apple2Plus, .apple2eEnhanced, .apple2c, .apple2gsROM01, .apple2gsROM03
        ]
        romValidation = [:]
        for machine in machinesToCheck {
            let result = MAMECommandBuilder.validateROMs(for: machine, romPath: romURL)
            romValidation[machine] = result.isValid
        }
        isValidating = false
    }
}

// MARK: - Display Settings

struct DisplaySettingsView: View {
    var body: some View {
        Form {
            Section("Default Display Settings") {
                Text("These defaults apply to new machine profiles.")
                    .foregroundStyle(.secondary)
                    .font(.callout)

                // Placeholder — will be fully wired in Phase 2
                LabeledContent("Filter") { Text("Sharp Pixels") }
                LabeledContent("Aspect Ratio") { Text("4:3 (Original)") }
                LabeledContent("Window Mode") { Text("Windowed") }
                LabeledContent("Zoom") { Text("2x") }
            }
        }
        .padding()
    }
}

// MARK: - Input Settings

struct InputSettingsView: View {
    var body: some View {
        Form {
            Section("Keyboard Mapping") {
                Text("Default keyboard mappings for Apple II keys.")
                    .foregroundStyle(.secondary)
                    .font(.callout)

                LabeledContent("Open Apple") { Text("Left Option") }
                LabeledContent("Closed Apple") { Text("Right Option") }
                LabeledContent("Reset") { Text("Ctrl+Cmd+R") }
            }

            Section("Joystick") {
                LabeledContent("Source") { Text("Keyboard (Arrow Keys)") }
                LabeledContent("Numpad as Joystick") { Text("Off") }
            }
        }
        .padding()
    }
}
