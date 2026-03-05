import SwiftUI
import UniformTypeIdentifiers

/// Sheet presented when launching a disk image — lets the user pick a machine profile
/// and optionally a second disk, then fires off the MAME session.
struct QuickLaunchView: View {
    @EnvironmentObject var appState: AppState
    @Environment(\.dismiss) private var dismiss

    let item: LibraryItem
    let onLaunch: (MachineProfile, [MediaSlot: URL]) -> Void

    @State private var selectedProfileID: UUID?
    @State private var secondDisk: URL?
    @State private var showFilePicker = false

    // Suggest a sensible default profile based on media type
    private var suggestedProfiles: [MachineProfile] {
        let profiles = appState.configStore.savedProfiles()
        // If the disk is .hdv, prefer profiles with a CFFA or SCSI card
        if item.mediaType == .hdv {
            let hdProfiles = profiles.filter { profile in
                profile.slots.values.contains(where: { card in
                    [.cffa2, .cffa202, .scsi, .hsscsi, .vulcan, .vulcanGold].contains(card)
                })
            }
            if !hdProfiles.isEmpty {
                return hdProfiles + profiles.filter { !hdProfiles.contains($0) }
            }
        }
        return profiles
    }

    private var selectedProfile: MachineProfile? {
        suggestedProfiles.first { $0.id == selectedProfileID }
    }

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Image(systemName: "play.circle.fill")
                    .font(.title)
                    .foregroundStyle(.tint)
                VStack(alignment: .leading, spacing: 2) {
                    Text("Quick Launch")
                        .font(.title3)
                        .fontWeight(.semibold)
                    Text(item.title)
                        .font(.callout)
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                }
                Spacer()
            }
            .padding()

            Divider()

            // Machine Profile Selection
            VStack(alignment: .leading, spacing: 12) {
                Text("Choose a Machine")
                    .font(.headline)

                List(suggestedProfiles, selection: $selectedProfileID) { profile in
                    HStack {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(profile.name)
                                .fontWeight(.medium)
                            Text(profile.machineType.displayName)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        Spacer()
                        // Show slot summary
                        let cardCount = profile.slots.values.filter { $0 != .empty }.count
                        if cardCount > 0 {
                            Text("\(cardCount) cards")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 2)
                                .background(.quaternary)
                                .clipShape(Capsule())
                        }
                    }
                    .tag(profile.id)
                }
                .listStyle(.bordered(alternatesRowBackgrounds: true))
                .frame(minHeight: 180)
            }
            .padding()

            // Second disk (optional)
            VStack(alignment: .leading, spacing: 8) {
                Text("Second Disk (Optional)")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)

                HStack {
                    if let url = secondDisk {
                        Label(url.lastPathComponent, systemImage: "opticaldisc")
                            .lineLimit(1)
                            .truncationMode(.middle)
                        Button(action: { secondDisk = nil }) {
                            Image(systemName: "xmark.circle.fill")
                                .foregroundStyle(.secondary)
                        }
                        .buttonStyle(.plain)
                    } else {
                        Text("None")
                            .foregroundStyle(.tertiary)
                    }
                    Spacer()
                    Button("Browse...") {
                        showFilePicker = true
                    }
                }
            }
            .padding(.horizontal)
            .padding(.bottom)

            Divider()

            // Command preview
            if let profile = selectedProfile,
               let config = appState.configStore.mameConfig() {
                let media = buildMedia()
                let cmdStr = MAMECommandBuilder.commandString(
                    binary: config.mameBinaryURL,
                    machine: profile,
                    media: media,
                    config: config
                )
                VStack(alignment: .leading, spacing: 4) {
                    Text("Command Preview")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    ScrollView(.horizontal, showsIndicators: false) {
                        Text(cmdStr)
                            .font(.system(.caption2, design: .monospaced))
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                    }
                }
                .padding(.horizontal)
                .padding(.vertical, 8)

                Divider()
            }

            // Action buttons
            HStack {
                Button("Cancel") {
                    dismiss()
                }
                .keyboardShortcut(.cancelAction)

                Spacer()

                Button("Launch") {
                    guard let profile = selectedProfile else { return }
                    onLaunch(profile, buildMedia())
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .disabled(selectedProfile == nil)
                .keyboardShortcut(.defaultAction)
            }
            .padding()
        }
        .frame(width: 520, height: 500)
        .fileImporter(
            isPresented: $showFilePicker,
            allowedContentTypes: [.data],
            allowsMultipleSelection: false
        ) { result in
            if case .success(let urls) = result, let url = urls.first {
                secondDisk = url
            }
        }
        .onAppear {
            // Auto-select first profile
            if selectedProfileID == nil {
                selectedProfileID = suggestedProfiles.first?.id
            }
        }
    }

    private func buildMedia() -> [MediaSlot: URL] {
        var media: [MediaSlot: URL] = [:]

        // Primary media slot based on type
        if item.mediaType == .hdv {
            media[.hard1] = item.url
        } else {
            media[.floppy1] = item.url
        }

        // Optional second disk
        if let url = secondDisk {
            if item.mediaType == .hdv {
                media[.hard2] = url
            } else {
                media[.floppy2] = url
            }
        }

        return media
    }
}
