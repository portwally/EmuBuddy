import SwiftUI
import UniformTypeIdentifiers

/// Main window: sidebar navigation + detail content.
struct ContentView: View {
    @EnvironmentObject var appState: AppState
    @State private var showSetupWizard = false
    @State private var showLaunchSheet = false
    @State private var launchItem: LibraryItem?
    @State private var showErrorAlert = false
    @State private var showMetadataEditor = false
    @State private var editingItem: LibraryItem?

    /// Quick-launch: picks a compatible profile and boots directly.
    /// For HDV (hard drive) images, selects a profile that has a hard drive controller card.
    /// Hold Option (⌥) or right-click → "Launch with..." for the profile picker sheet.
    private func handleLaunch(_ item: LibraryItem) {
        print("[EmuBuddy] handleLaunch: \(item.title) (\(item.url.lastPathComponent))")
        let profiles = appState.configStore.savedProfiles()
        guard !profiles.isEmpty else {
            // No profiles at all — show the picker so user can see the issue
            launchItem = item
            showLaunchSheet = true
            return
        }

        // Build media mapping
        var media: [MediaSlot: URL] = [:]
        let needsHardDrive = item.mediaType == .hdv
        if needsHardDrive {
            media[.hard1] = item.url
        } else {
            media[.floppy1] = item.url
        }

        // Select the best profile for the media type.
        // Priority: (1) selected in Machines tab, (2) last launched, (3) best match.
        let machinesTabID = appState.selectedMachineProfileID
        let lastUsedID = appState.configStore.lastUsedProfileID
        // Preferred ID: what the user selected in the Machines tab, falling back to last launched
        let preferredID = machinesTabID ?? lastUsedID

        print("[EmuBuddy] selectedMachineProfileID: \(machinesTabID?.uuidString.prefix(8) ?? "nil")")
        print("[EmuBuddy] lastUsedProfileID: \(lastUsedID?.uuidString.prefix(8) ?? "nil")")
        print("[EmuBuddy] preferredID: \(preferredID?.uuidString.prefix(8) ?? "nil")")
        print("[EmuBuddy] Available profiles (\(profiles.count)):")
        for p in profiles {
            let hdCapable = p.hasHardDriveController ? " [HD]" : ""
            print("[EmuBuddy]   • \(p.name) [id=\(p.id.uuidString.prefix(8))...]\(hdCapable)")
        }
        let selectedProfile: MachineProfile

        if needsHardDrive {
            // For HDV files, we need a profile with a hard drive controller (CFFA2, SCSI, etc.)
            if let prefID = preferredID,
               let prefProfile = profiles.first(where: { $0.id == prefID }),
               prefProfile.hasHardDriveController {
                selectedProfile = prefProfile
                print("[EmuBuddy] Using preferred HD-capable profile: \(prefProfile.name)")
            } else if let hdProfile = profiles.first(where: { $0.hasHardDriveController }) {
                selectedProfile = hdProfile
                print("[EmuBuddy] Selected HD-capable profile: \(hdProfile.name)")
            } else {
                // No profile has a hard drive controller — show profile picker
                print("[EmuBuddy] No HD-capable profile found, showing profile picker")
                launchItem = item
                showLaunchSheet = true
                return
            }
        } else {
            // For floppy images: prefer selected/last-used profile, else first profile
            if let prefID = preferredID,
               let prefProfile = profiles.first(where: { $0.id == prefID }) {
                selectedProfile = prefProfile
                print("[EmuBuddy] Using preferred profile: \(prefProfile.name)")
            } else {
                selectedProfile = profiles.first!
            }
        }

        print("[EmuBuddy] Quick-launching with profile: \(selectedProfile.name), media: \(item.url.lastPathComponent)")
        Task {
            await appState.launchSession(profile: selectedProfile, media: media)
        }
    }

    /// Show the full profile picker sheet for advanced launches.
    private func handleLaunchWithOptions(_ item: LibraryItem) {
        print("[EmuBuddy] handleLaunchWithOptions: \(item.title)")
        launchItem = item
        showLaunchSheet = true
    }

    var body: some View {
        Group {
            if let session = appState.activeSession {
                EmulationSessionView(session: session)
            } else {
                NavigationSplitView {
                    SidebarView(selection: $appState.selectedSidebarItem)
                } detail: {
                    switch appState.selectedSidebarItem {
                    case .library:
                        LibraryBrowserView(onLaunch: handleLaunch, onLaunchWithOptions: handleLaunchWithOptions)
                    case .machines:
                        MachineListView()
                    case .recentlyPlayed:
                        RecentlyPlayedView(onLaunch: handleLaunch)
                    case .favorites:
                        FavoritesView(onLaunch: handleLaunch)
                    case .settings:
                        SettingsView()
                    }
                }
            }
        }
        .navigationTitle("EmuBuddy")
        .frame(minWidth: 900, minHeight: 600)
        .sheet(isPresented: $showSetupWizard) {
            SetupWizardView()
                .environmentObject(appState)
        }
        .sheet(isPresented: $showLaunchSheet) {
            if let item = launchItem {
                QuickLaunchView(item: item) { profile, media in
                    print("[EmuBuddy] QuickLaunch callback — profile: \(profile.name), media count: \(media.count)")
                    showLaunchSheet = false
                    Task {
                        await appState.launchSession(profile: profile, media: media)
                    }
                }
                .environmentObject(appState)
            }
        }
        .sheet(isPresented: $showMetadataEditor) {
            if let item = editingItem {
                MetadataEditorView(item: item) { updates in
                    appState.libraryService.updateMetadata(
                        for: item,
                        title: updates.title,
                        publisher: updates.publisher,
                        year: updates.year,
                        genre: updates.genre,
                        tags: updates.tags
                    )
                    showMetadataEditor = false
                }
                .environmentObject(appState)
            }
        }
        .alert("Launch Error", isPresented: $showErrorAlert) {
            Button("OK") {
                appState.launchError = nil
            }
        } message: {
            Text(appState.launchError ?? "An unknown error occurred.")
        }
        .onAppear {
            if !appState.isMAMEConfigured {
                showSetupWizard = true
            }
            // Note: Library scan is triggered by LibraryBrowserView's .task modifier
            // to avoid double-scanning and "Publishing changes" warnings.
        }
        .onChange(of: appState.isMAMEConfigured) { _, newValue in
            if newValue {
                showSetupWizard = false
            }
        }
        .onChange(of: appState.launchError) { _, newError in
            if newError != nil {
                showErrorAlert = true
            }
        }
        // Handle menu bar notification commands
        .onReceive(NotificationCenter.default.publisher(for: .emubuddyOpenDiskImage)) { _ in
            let panel = NSOpenPanel()
            panel.title = "Open Disk Image"
            panel.canChooseFiles = true
            panel.canChooseDirectories = false
            panel.allowsMultipleSelection = false
            panel.allowedContentTypes = [.data]
            if panel.runModal() == .OK, let url = panel.url {
                let ext = url.pathExtension.lowercased()
                if let mediaType = MediaType.from(extension: ext) {
                    let item = LibraryItem(url: url, mediaType: mediaType)
                    handleLaunch(item)
                }
            }
        }
    }
}

// MARK: - Sidebar

struct SidebarView: View {
    @Binding var selection: SidebarItem
    @EnvironmentObject var appState: AppState

    var body: some View {
        List(SidebarItem.allCases, selection: $selection) { item in
            Label {
                HStack {
                    Text(item.rawValue)
                    Spacer()
                    // Show badge counts
                    switch item {
                    case .library:
                        let count = appState.libraryService.items.count
                        if count > 0 {
                            Text("\(count)")
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                        }
                    case .recentlyPlayed:
                        let count = appState.libraryService.items.filter { $0.lastPlayed != nil }.count
                        if count > 0 {
                            Text("\(count)")
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                        }
                    case .favorites:
                        let count = appState.libraryService.items.filter(\.isFavorite).count
                        if count > 0 {
                            Text("\(count)")
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                        }
                    default:
                        EmptyView()
                    }
                }
            } icon: {
                Image(systemName: item.systemImage)
            }
            .tag(item)
        }
        .listStyle(.sidebar)
        .frame(minWidth: 180)
    }
}

// MARK: - Metadata Editor

struct MetadataEditorView: View {
    @Environment(\.dismiss) private var dismiss

    let item: LibraryItem
    let onSave: (MetadataUpdates) -> Void

    @State private var title: String
    @State private var publisher: String
    @State private var yearString: String
    @State private var genre: Genre?
    @State private var tagsString: String

    init(item: LibraryItem, onSave: @escaping (MetadataUpdates) -> Void) {
        self.item = item
        self.onSave = onSave
        _title = State(initialValue: item.title)
        _publisher = State(initialValue: item.publisher ?? "")
        _yearString = State(initialValue: item.year != nil ? "\(item.year!)" : "")
        _genre = State(initialValue: item.genre)
        _tagsString = State(initialValue: item.tags.sorted().joined(separator: ", "))
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Text("Edit Metadata")
                    .font(.title3)
                    .fontWeight(.semibold)
                Spacer()
            }
            .padding()

            Divider()

            Form {
                TextField("Title", text: $title)

                TextField("Publisher", text: $publisher)

                TextField("Year", text: $yearString)

                Picker("Genre", selection: $genre) {
                    Text("None").tag(Genre?.none)
                    ForEach(Genre.allCases, id: \.self) { g in
                        Text(g.rawValue).tag(Genre?.some(g))
                    }
                }

                TextField("Tags (comma-separated)", text: $tagsString)

                // Read-only info
                Section("File Info") {
                    LabeledContent("File", value: item.url.lastPathComponent)
                    LabeledContent("Format", value: item.mediaType.displayName)
                    LabeledContent("Size", value: ByteCountFormatter.string(fromByteCount: item.fileSize, countStyle: .file))
                    if item.playCount > 0 {
                        LabeledContent("Play Count", value: "\(item.playCount)")
                    }
                    if let date = item.lastPlayed {
                        LabeledContent("Last Played") {
                            Text(date, style: .relative)
                        }
                    }
                }
            }
            .formStyle(.grouped)
            .padding()

            Divider()

            HStack {
                Button("Cancel") { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Spacer()
                Button("Save") {
                    let tags: Set<String>? = tagsString.isEmpty ? nil :
                        Set(tagsString.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) })
                    onSave(MetadataUpdates(
                        title: title != item.title ? title : nil,
                        publisher: publisher.isEmpty ? nil : publisher,
                        year: Int(yearString),
                        genre: genre,
                        tags: tags
                    ))
                }
                .buttonStyle(.borderedProminent)
                .keyboardShortcut(.defaultAction)
            }
            .padding()
        }
        .frame(width: 450, height: 500)
    }
}

struct MetadataUpdates {
    var title: String?
    var publisher: String?
    var year: Int?
    var genre: Genre?
    var tags: Set<String>?
}
