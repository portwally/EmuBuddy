import Foundation
import Combine

/// Represents a running MAME emulation session.
@MainActor
final class EmulationSession: ObservableObject, Identifiable {
    let id: UUID
    let machineProfile: MachineProfile
    @Published var media: [MediaSlot: URL]
    @Published var status: SessionStatus
    @Published var startedAt: Date
    @Published var processID: Int32?

    init(
        id: UUID = UUID(),
        machineProfile: MachineProfile,
        media: [MediaSlot: URL],
        status: SessionStatus = .starting
    ) {
        self.id = id
        self.machineProfile = machineProfile
        self.media = media
        self.status = status
        self.startedAt = Date()
    }
}

enum SessionStatus: String {
    case starting   = "Starting..."
    case running    = "Running"
    case paused     = "Paused"
    case stopped    = "Stopped"
    case error      = "Error"
}

// MARK: - Save State

struct SaveState: Codable, Identifiable {
    let id: UUID
    var sessionMachineProfile: MachineProfile
    var media: [String: String]  // Simplified for Codable: slotRawValue -> filePath
    var slot: Int
    var timestamp: Date
    var thumbnailPath: String?
    var mameStateFilePath: String

    init(
        id: UUID = UUID(),
        machineProfile: MachineProfile,
        media: [MediaSlot: URL],
        slot: Int,
        mameStateFilePath: String
    ) {
        self.id = id
        self.sessionMachineProfile = machineProfile
        self.media = Dictionary(uniqueKeysWithValues: media.map { ($0.key.rawValue, $0.value.path) })
        self.slot = slot
        self.timestamp = Date()
        self.mameStateFilePath = mameStateFilePath
    }
}
