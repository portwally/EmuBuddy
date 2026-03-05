import SwiftUI

/// List and manage machine profiles (presets + custom).
struct MachineListView: View {
    @EnvironmentObject var appState: AppState
    @State private var profiles: [MachineProfile] = MachineProfile.presets
    @State private var selectedProfile: MachineProfile?
    @State private var showingEditor = false

    var body: some View {
        HSplitView {
            // Profile list
            List(profiles, selection: Binding(
                get: { selectedProfile?.id },
                set: { id in selectedProfile = profiles.first { $0.id == id } }
            )) { profile in
                MachineProfileRow(profile: profile)
                    .tag(profile.id)
            }
            .frame(minWidth: 250)

            // Detail / Editor
            if let profile = selectedProfile {
                MachineDetailView(profile: profile)
            } else {
                Text("Select a machine profile")
                    .foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button(action: { showingEditor = true }) {
                    Label("New Profile", systemImage: "plus")
                }
            }
        }
    }
}

struct MachineProfileRow: View {
    let profile: MachineProfile

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(profile.name)
                .fontWeight(.medium)
            Text(profile.machineType.displayName)
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        .padding(.vertical, 4)
    }
}

// MARK: - Machine Detail

struct MachineDetailView: View {
    let profile: MachineProfile

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
                    Button("Launch") {
                        // TODO: Launch with this profile
                    }
                    .controlSize(.large)
                    .buttonStyle(.borderedProminent)
                }

                Divider()

                // Configuration summary
                LabeledContent("Family") { Text(profile.machineType.family.rawValue) }
                LabeledContent("RAM") { Text(profile.ramSize.displayName) }
                LabeledContent("CPU Speed") { Text(profile.cpuSpeed.displayName) }

                // Slots
                if profile.machineType.hasExpansionSlots {
                    Text("Expansion Slots")
                        .font(.headline)

                    ForEach(profile.machineType.configurableSlots, id: \.self) { slot in
                        let card = profile.slots[slot] ?? .empty
                        HStack {
                            Text("Slot \(slot)")
                                .frame(width: 50, alignment: .leading)
                            Text(card.displayName)
                                .foregroundStyle(card == .empty ? .secondary : .primary)
                            if card != .empty {
                                Text("(\(card.category.rawValue))")
                                    .font(.caption)
                                    .foregroundStyle(.tertiary)
                            }
                        }
                    }
                } else {
                    Text("No user-configurable expansion slots")
                        .foregroundStyle(.secondary)
                        .font(.callout)
                }
            }
            .padding()
        }
    }
}
