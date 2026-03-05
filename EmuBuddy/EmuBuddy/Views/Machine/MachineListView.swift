import SwiftUI

/// List and manage machine profiles (presets + custom).
struct MachineListView: View {
    @EnvironmentObject var appState: AppState
    @State private var profiles: [MachineProfile] = []
    @State private var showingNewProfileEditor = false
    @State private var newProfile: MachineProfile?

    /// Uses AppState for selection so it persists across tab switches.
    private var selectedProfileID: Binding<UUID?> {
        $appState.selectedMachineProfileID
    }

    private var selectedProfileIndex: Int? {
        guard let id = appState.selectedMachineProfileID else { return nil }
        return profiles.firstIndex(where: { $0.id == id })
    }

    var body: some View {
        HSplitView {
            // Profile list
            List(profiles, selection: selectedProfileID) { profile in
                MachineProfileRow(profile: profile)
                    .tag(profile.id)
                    .contextMenu {
                        Button("Duplicate") {
                            duplicateProfile(profile)
                        }
                        Divider()
                        Button("Delete", role: .destructive) {
                            deleteProfile(profile)
                        }
                    }
            }
            .frame(minWidth: 220, idealWidth: 260, maxWidth: 320)

            // Detail — inline editor (no separate Edit button / sheet needed)
            if let idx = selectedProfileIndex {
                MachineInlineEditorView(
                    profile: Binding(
                        get: { profiles[idx] },
                        set: { newValue in
                            profiles[idx] = newValue
                            appState.configStore.saveProfiles(profiles)
                        }
                    ),
                    onLaunch: {
                        Task {
                            await appState.launchSession(profile: profiles[idx], media: [:])
                        }
                    }
                )
                .id(profiles[idx].id) // Reset editor state when switching profiles
                .frame(minWidth: 500, idealWidth: 600)
            } else {
                Text("Select a machine profile")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .frame(minWidth: 500)
            }
        }
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button(action: {
                    newProfile = MachineProfile(
                        name: "New Profile",
                        machineType: .apple2eEnhanced,
                        ramSize: .kb64,
                        slots: [6: .diskIIng]
                    )
                }) {
                    Label("New Profile", systemImage: "plus")
                }
            }
        }
        .sheet(item: $newProfile) { profile in
            MachineEditorView(
                profile: profile,
                isNew: true
            ) { savedProfile in
                profiles.append(savedProfile)
                appState.selectedMachineProfileID = savedProfile.id
                appState.configStore.saveProfiles(profiles)
            }
            .environmentObject(appState)
        }
        .onAppear {
            profiles = appState.configStore.savedProfiles()
            if appState.selectedMachineProfileID == nil {
                appState.selectedMachineProfileID = profiles.first?.id
            }
        }
    }

    private func duplicateProfile(_ profile: MachineProfile) {
        let copy = MachineProfile(
            name: "\(profile.name) Copy",
            machineType: profile.machineType,
            ramSize: profile.ramSize,
            cpuSpeed: profile.cpuSpeed,
            slots: profile.slots,
            gameIODevice: profile.gameIODevice,
            displaySettings: profile.displaySettings,
            inputMapping: profile.inputMapping
        )
        profiles.append(copy)
        appState.selectedMachineProfileID = copy.id
        appState.configStore.saveProfiles(profiles)
    }

    private func deleteProfile(_ profile: MachineProfile) {
        profiles.removeAll { $0.id == profile.id }
        if appState.selectedMachineProfileID == profile.id {
            appState.selectedMachineProfileID = profiles.first?.id
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

// MARK: - Inline Editor (replaces the old read-only detail + Edit sheet)

/// Editable machine profile shown directly in the detail panel.
/// Changes auto-save via the binding whenever a field is modified.
struct MachineInlineEditorView: View {
    @Binding var profile: MachineProfile
    let onLaunch: () -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                // Header with Boot button
                HStack(alignment: .center) {
                    VStack(alignment: .leading, spacing: 2) {
                        TextField("Profile Name", text: $profile.name)
                            .font(.title2)
                            .fontWeight(.bold)
                            .textFieldStyle(.plain)

                        Text(profile.machineType.displayName)
                            .foregroundStyle(.secondary)
                    }

                    Spacer()

                    Button(action: onLaunch) {
                        Label("Boot", systemImage: "power")
                    }
                    .controlSize(.large)
                    .buttonStyle(.borderedProminent)
                }
                .padding()

                Divider()

                // Two-column layout: settings on left, slots on right
                HStack(alignment: .top, spacing: 0) {
                    // Left column: General / Input / Display settings
                    Form {
                        Section("General") {
                            Picker("RAM", selection: $profile.ramSize) {
                                ForEach(RAMSize.validSizes(for: profile.machineType), id: \.self) { size in
                                    Text(size.displayName).tag(size)
                                }
                            }

                            Picker("CPU Speed", selection: $profile.cpuSpeed) {
                                ForEach(CPUSpeed.allCases) { speed in
                                    Text(speed.displayName).tag(speed)
                                }
                            }
                        }

                        Section("Input") {
                            Picker("Game I/O Device", selection: $profile.gameIODevice) {
                                Text("None").tag(GameIODevice?.none)
                                ForEach(GameIODevice.allCases) { device in
                                    Text(device.displayName).tag(GameIODevice?.some(device))
                                }
                            }

                            Picker("Joystick Source", selection: $profile.inputMapping.joystickSource) {
                                ForEach(JoystickSource.allCases, id: \.self) { source in
                                    Text(source.displayName).tag(source)
                                }
                            }
                        }

                        Section("Display") {
                            Picker("Filter", selection: $profile.displaySettings.filter) {
                                ForEach(DisplayFilter.allCases, id: \.self) { filter in
                                    Text(filter.displayName).tag(filter)
                                }
                            }

                            Picker("Aspect Ratio", selection: $profile.displaySettings.aspectRatio) {
                                ForEach(AspectRatio.allCases, id: \.self) { ratio in
                                    Text(ratio.displayName).tag(ratio)
                                }
                            }

                            Picker("Window Mode", selection: $profile.displaySettings.windowMode) {
                                ForEach(WindowMode.allCases, id: \.self) { mode in
                                    Text(mode.displayName).tag(mode)
                                }
                            }

                            Picker("Color", selection: $profile.displaySettings.colorMode) {
                                ForEach(ColorMode.allCases, id: \.self) { mode in
                                    Text(mode.displayName).tag(mode)
                                }
                            }
                        }
                    }
                    .formStyle(.grouped)
                    .frame(minWidth: 280, idealWidth: 320)
                    .scrollDisabled(true) // Parent ScrollView handles scrolling

                    Divider()

                    // Right column: Expansion Slots
                    if profile.machineType.hasExpansionSlots {
                        SlotConfiguratorView(
                            slots: $profile.slots,
                            configurableSlots: profile.machineType.configurableSlots
                        )
                        .frame(minWidth: 280, idealWidth: 320)
                    } else {
                        VStack(spacing: 12) {
                            Image(systemName: "rectangle.slash")
                                .font(.system(size: 36))
                                .foregroundStyle(.secondary)
                            Text("No Expansion Slots")
                                .font(.headline)
                                .foregroundStyle(.secondary)
                            Text("The \(profile.machineType.displayName) has no\nuser-configurable expansion slots.")
                                .font(.callout)
                                .foregroundStyle(.tertiary)
                                .multilineTextAlignment(.center)
                        }
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .frame(minWidth: 280)
                    }
                }
            }
        }
        .onChange(of: profile.machineType) { _, newType in
            // Reset RAM to a valid value when machine type changes
            let valid = RAMSize.validSizes(for: newType)
            if !valid.contains(profile.ramSize) {
                profile.ramSize = valid.first ?? .kb64
            }
            // Clear slots for machines without expansion
            if !newType.hasExpansionSlots {
                profile.slots = [:]
            }
        }
    }
}
