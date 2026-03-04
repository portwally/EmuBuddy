import SwiftUI
import UniformTypeIdentifiers

/// Toolbar and controls shown while an emulation session is active.
struct EmulationSessionView: View {
    @ObservedObject var session: EmulationSession
    @EnvironmentObject var appState: AppState

    var body: some View {
        VStack(spacing: 0) {
            // Session toolbar
            HStack {
                // Machine info
                VStack(alignment: .leading, spacing: 2) {
                    Text(session.machineProfile.name)
                        .fontWeight(.medium)
                    Text(statusText)
                        .font(.caption)
                        .foregroundStyle(statusColor)
                }

                Spacer()

                // Drive slots
                ForEach(Array(session.media.keys.sorted(by: { $0.rawValue < $1.rawValue })), id: \.self) { slot in
                    DriveSlotView(slot: slot, imageURL: session.media[slot])
                }

                Spacer()

                // Controls
                HStack(spacing: 12) {
                    Button(action: { /* TODO: Save state */ }) {
                        Label("Save", systemImage: "square.and.arrow.down")
                    }

                    Button(action: { /* TODO: Load state */ }) {
                        Label("Load", systemImage: "square.and.arrow.up")
                    }

                    Button(action: { /* TODO: Reset */ }) {
                        Label("Reset", systemImage: "arrow.counterclockwise")
                    }

                    Button(role: .destructive, action: {
                        Task {
                            await appState.mameEngine.terminate(session: session)
                            appState.activeSession = nil
                        }
                    }) {
                        Label("Stop", systemImage: "stop.fill")
                    }
                }
            }
            .padding(.horizontal)
            .padding(.vertical, 8)
            .background(.bar)

            Divider()

            // MAME runs in its own window — this area could show
            // session info, save state browser, or disk swap panel
            Text("MAME is running in a separate window.")
                .font(.title3)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }

    var statusText: String {
        session.status.rawValue
    }

    var statusColor: Color {
        switch session.status {
        case .running: return .green
        case .starting: return .orange
        case .paused: return .yellow
        case .stopped: return .secondary
        case .error: return .red
        }
    }
}

// MARK: - Drive Slot

struct DriveSlotView: View {
    let slot: MediaSlot
    let imageURL: URL?

    var body: some View {
        VStack(spacing: 2) {
            Image(systemName: "opticaldisc")
                .font(.title3)
            Text(slot.displayName)
                .font(.caption2)
            if let url = imageURL {
                Text(url.lastPathComponent)
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .frame(maxWidth: 100)
            } else {
                Text("Empty")
                    .font(.caption2)
                    .foregroundStyle(.tertiary)
            }
        }
        .padding(8)
        .background(.quaternary)
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .onDrop(of: [.fileURL], isTargeted: nil) { providers in
            // TODO: Handle disk image drop for hot-swap
            return false
        }
    }
}
