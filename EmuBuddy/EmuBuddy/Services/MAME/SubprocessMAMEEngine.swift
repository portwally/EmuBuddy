import Foundation

/// Phase 1 MAME engine: launches MAME as a child process.
final class SubprocessMAMEEngine: MAMEEngine, @unchecked Sendable {

    private let configStore: ConfigStore
    private var activeProcess: Process?
    private var statusContinuation: AsyncStream<EngineStatus>.Continuation?

    var statusStream: AsyncStream<EngineStatus> {
        AsyncStream { continuation in
            self.statusContinuation = continuation
        }
    }

    init(config: ConfigStore) {
        self.configStore = config
    }

    // MARK: - Launch

    func launch(
        machine: MachineProfile,
        media: [MediaSlot: URL],
        config: MAMEConfig
    ) async throws -> EmulationSession {

        // Validate MAME binary exists
        guard FileManager.default.fileExists(atPath: config.mameBinaryURL.path) else {
            throw MAMEEngineError.mameBinaryNotFound(config.mameBinaryURL)
        }

        // Build command-line arguments
        let arguments = buildArguments(machine: machine, media: media, config: config)

        // Create and configure the process
        let process = Process()
        process.executableURL = config.mameBinaryURL
        process.arguments = arguments

        // Capture stdout and stderr
        let stdoutPipe = Pipe()
        let stderrPipe = Pipe()
        process.standardOutput = stdoutPipe
        process.standardError = stderrPipe

        // Set up termination handler
        process.terminationHandler = { [weak self] proc in
            self?.statusContinuation?.yield(.terminated(exitCode: proc.terminationStatus))
        }

        // Launch
        statusContinuation?.yield(.launching)

        do {
            try process.run()
        } catch {
            throw MAMEEngineError.launchFailed(error.localizedDescription)
        }

        activeProcess = process
        statusContinuation?.yield(.running(processID: process.processIdentifier))

        // Create session
        let session = await EmulationSession(
            machineProfile: machine,
            media: media,
            status: .running
        )
        await MainActor.run {
            session.processID = process.processIdentifier
        }

        // Monitor stderr for errors in background
        Task.detached { [weak self] in
            self?.monitorStderr(pipe: stderrPipe)
        }

        return session
    }

    // MARK: - Terminate

    func terminate(session: EmulationSession) async {
        activeProcess?.terminate()
        activeProcess = nil
        await MainActor.run {
            session.status = .stopped
        }
    }

    // MARK: - Save / Load State

    func saveState(session: EmulationSession, slot: Int) async throws -> SaveState {
        // MAME save state: send Shift+F7 then slot number via stdin or IPC
        // For Phase 1, we rely on MAME's built-in keyboard shortcuts
        // TODO: Implement via MAME's IPC socket
        throw MAMEEngineError.saveStateFailed("Not yet implemented for subprocess mode")
    }

    func loadState(session: EmulationSession, state: SaveState) async throws {
        // MAME load state: send F7 then slot number
        // TODO: Implement via MAME's IPC socket
        throw MAMEEngineError.saveStateFailed("Not yet implemented for subprocess mode")
    }

    // MARK: - Media Swap

    func swapMedia(session: EmulationSession, slot: MediaSlot, image: URL?) async throws {
        // Phase 1: Would need to use MAME's internal UI (Scroll Lock → File Manager)
        // or MAME's plugin system / socket interface
        // TODO: Implement via MAME's IPC socket or natural keyboard input
        throw MAMEEngineError.mediaSwapFailed("Not yet implemented for subprocess mode")
    }

    // MARK: - Input

    func sendInput(session: EmulationSession, input: EmulatorInput) async {
        // Phase 1: Input goes directly to MAME's window (not through our process)
        // This becomes useful in Phase 2 (libMAME) where we inject input directly
    }

    // MARK: - Command Line Builder

    /// Builds MAME command-line arguments from a machine profile and media selection.
    func buildArguments(
        machine: MachineProfile,
        media: [MediaSlot: URL],
        config: MAMEConfig
    ) -> [String] {
        var args: [String] = []

        // Machine driver
        args.append(machine.machineType.mameDriver)

        // ROM path
        args.append(contentsOf: ["-rompath", config.romPath.path])

        // Config / state / snapshot paths
        args.append(contentsOf: ["-cfg_directory", config.cfgPath.path])
        args.append(contentsOf: ["-nvram_directory", config.nvramPath.path])
        args.append(contentsOf: ["-state_directory", config.statePath.path])
        args.append(contentsOf: ["-snapshot_directory", config.snapshotPath.path])

        // RAM size
        args.append(contentsOf: ["-ramsize", machine.ramSize.mameValue])

        // CPU speed
        args.append(contentsOf: ["-speed", String(machine.cpuSpeed.mameSpeedValue)])

        // Slot assignments
        for (slotNum, card) in machine.slots.sorted(by: { $0.key < $1.key }) {
            if card != .empty {
                args.append(contentsOf: ["-sl\(slotNum)", card.mameDevice])
            }
        }

        // Media
        for (slot, url) in media {
            args.append(contentsOf: [slot.mameFlag, url.path])
        }

        // Display
        switch machine.displaySettings.windowMode {
        case .windowed:
            args.append("-window")
        case .fullscreen:
            break  // MAME default is fullscreen
        case .fullscreenWindow:
            args.append(contentsOf: ["-window", "-maximize"])
        }

        // Display filter
        if machine.displaySettings.filter != .sharp {
            args.append(contentsOf: [
                "-video", "bgfx",
                "-bgfx_screen_chains", machine.displaySettings.filter.mameBGFXChain
            ])
        }

        return args
    }

    // MARK: - Monitoring

    private func monitorStderr(pipe: Pipe) {
        let data = pipe.fileHandleForReading.readDataToEndOfFile()
        if let output = String(data: data, encoding: .utf8), !output.isEmpty {
            // Log stderr output for debugging
            print("[MAME stderr] \(output)")
        }
    }
}
