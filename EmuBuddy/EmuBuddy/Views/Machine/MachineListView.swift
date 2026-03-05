import SwiftUI

/// List and manage machine profiles (presets + custom).
struct MachineListView: View {
    @EnvironmentObject var appState: AppState
    @State private var profiles: [MachineProfile] = []
    @State private var selectedProfileID: UUID?
    @State private var showingEditor = false
    @State private var editingProfile: MachineProfile?
    @State private var showDeleteConfirm = false

    private var selectedProfile: MachineProfile? {
        profiles.first { $0.id == selectedProfileID }
    }

    var body: some View {
        HSplitView {
            // Profile list
            List(profiles, selection: $selectedProfileID) { profile in
                MachineProfileRow(profile: profile)
                    .tag(profile.id)
                    .contextMenu {
                        Button("Edit...") {
                            editingProfile = profile
                        }
                        Button("Duplicate") {
                            duplicateProfile(profile)
                        }
                        Divider()
                        Button("Delete", role: .destructive) {
                            deleteProfile(profile)
                        }
                    }
            }
            .frame(minWidth: 250)

            // Detail
            if let profile = selectedProfile {
                MachineDetailView(
                    profile: profile,
                    onEdit: {
                        editingProfile = profile
                    },
                    onLaunch: {
                        Task {
                            await appState.launchSession(profile: profile, media: [:])
                        }
                    }
                )
            } else {
                Text("Select a machine profile")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button(action: {
                    let newProfile = MachineProfile(
                        name: "New Profile",
                        machineType: .apple2eEnhanced,
                        ramSize: .kb128,
                        slots: [6: .diskIIng]
                    )
                    editingProfile = newProfile
                    showingEditor = true
                }) {
                    Label("New Profile", systemImage: "plus")
                }
            }
        }
        .sheet(item: $editingProfile) { profile in
            MachineEditorView(
                profile: profile,
                isNew: !profiles.contains(where: { $0.id == profile.id })
            ) { savedProfile in
                if let idx = profiles.firstIndex(where: { $0.id == savedProfile.id }) {
                    profiles[idx] = savedProfile
                } else {
                    profiles.append(savedProfile)
                }
                selectedProfileID = savedProfile.id
                appState.configStore.saveProfiles(profiles)
            }
            .environmentObject(appState)
        }
        .onAppear {
            profiles = appState.configStore.savedProfiles()
            if selectedProfileID == nil {
                selectedProfileID = profiles.first?.id
            }
        }
    }

    private func duplicateProfile(_ profile: MachineProfile) {
        var copy = profile
        copy = MachineProfile(
            name: "\(profile.name) Copy",
            machineType: profile.machineType,
            ramSize: profile.ramSize,
            cpuSpeed: profile.cpuSpeed,
            slots: profile.slots,
            displaySettings: profile.displaySettings,
            inputMapping: profile.inputMapping
        )
        profiles.append(copy)
        selectedProfileID = copy.id
        appState.configStore.saveProfiles(profiles)
    }

    private func deleteProfile(_ profile: MachineProfile) {
        profiles.removeAll { $0.id == profile.id }
        if selectedProfileID == profile.id {
            selectedProfileID = profiles.first?.id
        }
        appState.configStore.saveProfiles(profiles)
    }
}

struct MachineProfileRow: View {
    let profile: MachineProfile

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(profile.name)
                .fontWeight(.medium)
            HStack(spacing: 4) {
                Text(profile.machineType.displayName)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Text("·")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
                Text(profile.ramSize.displayName)
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
        }
        .padding(.vertical, 4)
    }
}

// MARK: - Machine Detail

struct MachineDetailView: View {
    let profile: MachineProfile
    let onEdit: () -> Void
    let onLaunch: () -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                // Header
                HStack {
                    VStack(alignment: .leading) {
                        Text(profile.name)
                            .font(.title2)
                            .fontWeight(.bold)
                        Text(profile.machineType.displayName)
                            .foregroundStyle(.secondary)
                    }
                    Spacer()

                    Button("Edit", action: onEdit)
                        .controlSize(.regular)

                    Button(action: onLaunch) {
                        Label("Boot", systemImage: "power")
                    }
                    .controlSize(.large)
                    .buttonStyle(.borderedProminent)
                }

                Divider()

                // Configuration summary
                LazyVGrid(columns: [
                    GridItem(.flexible(), alignment: .leading),
                    GridItem(.flexible(), alignment: .leading)
                ], spacing: 12) {
                    LabeledContent("Family") { Text(profile.machineType.family.rawValue) }
                    LabeledContent("MAME Driver") {
                        Text(profile.machineType.mameDriver)
                            .font(.system(.caption, design: .monospaced))
                    }
                    LabeledContent("RAM") { Text(profile.ramSize.displayName) }
                    LabeledContent("CPU Speed") { Text(profile.cpuSpeed.displayName) }
                    LabeledContent("Display Filter") { Text(profile.displaySettings.filter.displayName) }
                    LabeledContent("Window Mode") { Text(profile.displaySettings.windowMode.displayName) }
                }

                // Slots
                if profile.machineType.hasExpansionSlots {
                    Text("Expansion Slots")
                        .font(.headline)

                    VStack(spacing: 6) {
                        ForEach(profile.machineType.configurableSlots, id: \.self) { slot in
                            let card = profile.slots[slot] ?? .empty
                            HStack {
                                Text("Slot \(slot)")
                                    .font(.system(.callout, design: .monospaced))
                                    .frame(width: 50, alignment: .leading)

                                if card != .empty {
                                    Image(systemName: slotIcon(card.category))
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }

                                Text(card.displayName)
                                    .foregroundStyle(card == .empty ? .tertiary : .primary)

                                if card != .empty {
                                    Spacer()
                                    Text(card.category.rawValue)
                                        .font(.caption2)
                                        .foregroundStyle(.tertiary)
                                        .padding(.horizontal, 6)
                                        .padding(.vertical, 2)
                                        .background(.quaternary)
                                        .clipShape(Capsule())
                                }
                            }
                        }
                    }
                } else {
                    HStack(spacing: 8) {
                        Image(systemName: "info.circle")
                            .foregroundStyle(.secondary)
                        Text("The \(profile.machineType.displayName) has no user-configurable expansion slots.")
                            .foregroundStyle(.secondary)
                            .font(.callout)
                    }
                }
            }
            .padding()
        }
    }

    private func slotIcon(_ category: SlotCardCategory) -> String {
        switch category {
        case .diskStorage: return "internaldrive"
        case .audio: return "speaker.wave.2"
        case .serialParallel: return "cable.connector"
        case .memory: return "memorychip"
        case .video: return "display"
        case .coprocessor: return "cpu"
        case .input: return "computermouse"
        case .network: return "network"
        case .other: return "puzzlepiece"
        }
    }
}
