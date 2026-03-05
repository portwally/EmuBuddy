import SwiftUI
import UniformTypeIdentifiers

/// Toolbar and controls shown while an emulation session is active.
struct EmulationSessionView: View {
    @ObservedObject var session: EmulationSession
    @EnvironmentObject var appState: AppState
    @State private var showDiskSwap: MediaSlot?
    @State private var elapsedTime: TimeInterval = 0
    @State private var timer: Timer?

    var body: some View {
        VStack(spacing: 0) {
            // Session toolbar
            HStack {
                // Machine info
                VStack(alignment: .leading, spacing: 2) {
                    Text(session.machineProfile.name)
                        .fontWeight(.medium)
                    HStack(spacing: 6) {
                        Circle()
                            .fill(statusColor)
                            .frame(width: 8, height: 8)
                        Text(statusText)
                            .font(.caption)
                            .foregroundStyle(statusColor)
                        Text("·")
                            .foregroundStyle(.tertiary)
                        Text(formattedElapsed)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .monospacedDigit()
                    }
                }

                Spacer()

                // Drive slots
                ForEach(Array(session.media.keys.sorted(by: { $0.rawValue < $1.rawValue })), id: \.self) { slot in
                    DriveSlotView(slot: slot, imageURL: session.media[slot]) {
                        showDiskSwap = slot
                    }
                }

                Spacer()

                // Controls
                HStack(spacing: 8) {
                    Button(action: {
                        MAMELuaCommand.saveState()
                    }) {
                        Label("Save", systemImage: "square.and.arrow.down")
                    }
                    .help("Quick Save (⌥⌘S)")

                    Button(action: {
                        MAMELuaCommand.loadState()
                    }) {
                        Label("Load", systemImage: "square.and.arrow.up")
                    }
                    .help("Quick Load (⌥⌘L)")

                    Divider()
                        .frame(height: 20)

                    Button(action: {
                        MAMELuaCommand.softReset()
                    }) {
                        Label("Reset", systemImage: "arrow.counterclockwise")
                    }
                    .help("Soft Reset (⌥⌘R)")

                    Button(role: .destructive, action: {
                        Task {
                            await appState.stopSession()
                        }
                    }) {
                        Label("Stop", systemImage: "stop.fill")
                    }
                    .help("Stop Emulation (⇧⌘Q)")
                }
            }
            .padding(.horizontal)
            .padding(.vertical, 8)
            .background(.bar)

            Divider()

            // Session info area
            VStack(spacing: 24) {
                Spacer()

                Image(systemName: "desktopcomputer")
                    .font(.system(size: 48))
                    .foregroundStyle(.secondary)

                Text("MAME is running in a separate window.")
                    .font(.title3)
                    .foregroundStyle(.secondary)

                // Session details
                VStack(alignment: .leading, spacing: 8) {
                    SessionInfoRow(label: "Machine", value: session.machineProfile.machineType.displayName)
                    SessionInfoRow(label: "Driver", value: session.machineProfile.machineType.mameDriver)
                    if let pid = session.processID {
                        SessionInfoRow(label: "Process ID", value: "\(pid)")
                    }
                    ForEach(Array(session.media.keys.sorted(by: { $0.rawValue < $1.rawValue })), id: \.self) { slot in
                        if let url = session.media[slot] {
                            SessionInfoRow(label: slot.displayName, value: url.lastPathComponent)
                        }
                    }
                    let cardCount = session.machineProfile.slots.values.filter { $0 != .empty }.count
                    if cardCount > 0 {
                        let cardNames = session.machineProfile.slots
                            .sorted(by: { $0.key < $1.key })
                            .filter { $0.value != .empty }
                            .map { "Slot \($0.key): \($0.value.displayName)" }
                            .joined(separator: ", ")
                        SessionInfoRow(label: "Cards", value: cardNames)
                    }
                }
                .padding()
                .background(.quaternary.opacity(0.5))
                .clipShape(RoundedRectangle(cornerRadius: 12))
                .frame(maxWidth: 400)

                Spacer()
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .onAppear { startTimer() }
        .onDisappear { stopTimer() }
        .fileImporter(
            isPresented: Binding(
                get: { showDiskSwap != nil },
                set: { if !$0 { showDiskSwap = nil } }
            ),
            allowedContentTypes: [.data],
            allowsMultipleSelection: false
        ) { result in
            if case .success(let urls) = result,
               let url = urls.first,
               let slot = showDiskSwap {
                Task {
                    try? await appState.mameEngine.swapMedia(session: session, slot: slot, image: url)
                    await MainActor.run {
                        session.media[slot] = url
                    }
                }
            }
            showDiskSwap = nil
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

    var formattedElapsed: String {
        let hours = Int(elapsedTime) / 3600
        let minutes = (Int(elapsedTime) % 3600) / 60
        let seconds = Int(elapsedTime) % 60
        if hours > 0 {
            return String(format: "%d:%02d:%02d", hours, minutes, seconds)
        }
        return String(format: "%d:%02d", minutes, seconds)
    }

    private func startTimer() {
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { _ in
            elapsedTime = Date().timeIntervalSince(session.startedAt)
        }
    }

    private func stopTimer() {
        timer?.invalidate()
        timer = nil
    }
}

// MARK: - Session Info Row

struct SessionInfoRow: View {
    let label: String
    let value: String

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Text(label)
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(width: 80, alignment: .trailing)
            Text(value)
                .font(.caption)
                .lineLimit(2)
        }
    }
}

// MARK: - Drive Slot

struct DriveSlotView: View {
    let slot: MediaSlot
    let imageURL: URL?
    var onSwap: (() -> Void)?

    var body: some View {
        Button(action: { onSwap?() }) {
            VStack(spacing: 2) {
                Image(systemName: slot.rawValue.contains("hard") ? "externaldrive.fill" : "opticaldisc")
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
        }
        .buttonStyle(.plain)
        .help("Click to swap disk")
        .onDrop(of: [.fileURL], isTargeted: nil) { providers in
            guard let provider = providers.first else { return false }
            _ = provider.loadObject(ofClass: URL.self) { url, _ in
                guard let url = url else { return }
                Task { @MainActor in
                    onSwap?()
                }
            }
            return true
        }
    }
}
