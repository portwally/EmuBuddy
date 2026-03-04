import Foundation

/// Protocol defining the MAME emulation engine interface.
/// Designed so SubprocessMAMEEngine (Phase 1) can be swapped for LibMAMEEngine (Phase 2)
/// without changing any calling code.
protocol MAMEEngine: Sendable {

    /// Launch an emulation session with the given machine, media, and configuration.
    func launch(
        machine: MachineProfile,
        media: [MediaSlot: URL],
        config: MAMEConfig
    ) async throws -> EmulationSession

    /// Terminate a running session.
    func terminate(session: EmulationSession) async

    /// Save the current emulation state.
    func saveState(session: EmulationSession, slot: Int) async throws -> SaveState

    /// Load a previously saved state.
    func loadState(session: EmulationSession, state: SaveState) async throws

    /// Swap media in a drive slot during an active session.
    func swapMedia(session: EmulationSession, slot: MediaSlot, image: URL?) async throws

    /// Send an input event to the emulator.
    func sendInput(session: EmulationSession, input: EmulatorInput) async

    /// Stream of engine status updates.
    var statusStream: AsyncStream<EngineStatus> { get }
}

// MARK: - Supporting Types

struct MAMEConfig {
    var mameBinaryURL: URL
    var romPath: URL
    var cfgPath: URL
    var nvramPath: URL
    var statePath: URL
    var snapshotPath: URL
}

enum EngineStatus: Sendable {
    case idle
    case launching
    case running(processID: Int32)
    case error(String)
    case terminated(exitCode: Int32)
}

enum EmulatorInput {
    case keyDown(keyCode: UInt16)
    case keyUp(keyCode: UInt16)
    case joystickAxis(x: Float, y: Float)
    case joystickButton(index: Int, pressed: Bool)
    case reset
    case pause
    case resume
}

// MARK: - Errors

enum MAMEEngineError: LocalizedError {
    case mameBinaryNotFound(URL)
    case romPathNotFound(URL)
    case launchFailed(String)
    case sessionNotRunning
    case saveStateFailed(String)
    case mediaSwapFailed(String)

    var errorDescription: String? {
        switch self {
        case .mameBinaryNotFound(let url):
            return "MAME binary not found at \(url.path)"
        case .romPathNotFound(let url):
            return "ROM directory not found at \(url.path)"
        case .launchFailed(let msg):
            return "Failed to launch MAME: \(msg)"
        case .sessionNotRunning:
            return "No active emulation session"
        case .saveStateFailed(let msg):
            return "Failed to save state: \(msg)"
        case .mediaSwapFailed(let msg):
            return "Failed to swap media: \(msg)"
        }
    }
}
