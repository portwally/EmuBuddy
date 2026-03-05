import Foundation
import AppKit

/// Phase 1 MAME engine: launches MAME as a child process.
final class SubprocessMAMEEngine: MAMEEngine, @unchecked Sendable {

    private let configStore: ConfigStore
    private var activeProcess: Process?
    private var statusContinuation: AsyncStream<EngineStatus>.Continuation?
    /// Captured stderr output from the last MAME run (read after process exits).
    private var lastStderrOutput: String = ""
    /// Captured stdout output from the last MAME run.
    private var lastStdoutOutput: String = ""
    /// Directory containing the EmuBuddy Lua plugin for MAME.
    private var pluginBaseDir: String?

    /// Eagerly-created status stream so the continuation exists before launch.
    let statusStream: AsyncStream<EngineStatus>

    init(config: ConfigStore) {
        self.configStore = config
        // Set up the stream eagerly so the continuation is ready before any launch.
        // Using makeStream (Swift 5.9+) avoids Sendable capture issues.
        let (stream, continuation) = AsyncStream.makeStream(of: EngineStatus.self)
        self.statusStream = stream
        self.statusContinuation = continuation
    }

    // MARK: - Launch

    func launch(
        machine: MachineProfile,
        media: [MediaSlot: URL],
        config: MAMEConfig
    ) async throws -> EmulationSession {

        // Use the original URL from config (preserves security-scoped bookmark access)
        let binaryURL = config.mameBinaryURL
        let binaryPath = binaryURL.path

        print("[EmuBuddy] Binary URL: \(binaryURL)")
        print("[EmuBuddy] Binary path: \(binaryPath)")
        print("[EmuBuddy] File exists: \(FileManager.default.fileExists(atPath: binaryPath))")

        // Start security-scoped access (needed for containerized/sandboxed apps)
        let didAccessBinary = binaryURL.startAccessingSecurityScopedResource()
        print("[EmuBuddy] Security-scoped access for binary: \(didAccessBinary)")

        // Validate MAME binary exists
        guard FileManager.default.fileExists(atPath: binaryPath) else {
            if didAccessBinary { binaryURL.stopAccessingSecurityScopedResource() }
            throw MAMEEngineError.mameBinaryNotFound(binaryURL)
        }

        // Build command-line arguments
        let arguments = buildArguments(machine: machine, media: media, config: config)

        // Log the full command for debugging
        let fullCmd = ([binaryPath] + arguments).joined(separator: " ")
        print("[EmuBuddy] Launching: \(fullCmd)")

        // Create and configure the process — use launchPath (string) instead of
        // executableURL to avoid Foundation URL resolution issues in sandboxed containers
        let process = Process()
        process.executableURL = URL(fileURLWithPath: binaryPath)
        process.arguments = arguments

        // Set working directory to the MAME binary's folder so MAME can resolve
        // relative paths (artwork/, bgfx/, plugins/, hash/, etc.) correctly.
        process.currentDirectoryURL = binaryURL.deletingLastPathComponent()

        // Set up environment for our EmuBuddy Lua plugin.
        // The plugin reads commands from a temp file; we set the path via env var.
        let cmdFilePath = NSTemporaryDirectory() + "emubuddy_cmd_\(UUID().uuidString).lua"
        var env = ProcessInfo.processInfo.environment
        env["EMUBUDDY_CMD_FILE"] = cmdFilePath
        process.environment = env

        // Capture stderr for error reporting.
        // IMPORTANT: Read pipes asynchronously to prevent deadlock — if MAME writes more
        // than the pipe buffer (64KB), it blocks until someone reads. Reading in the
        // termination handler would deadlock because termination waits for the process to exit.
        let stderrPipe = Pipe()
        process.standardError = stderrPipe

        // Send stdout to /dev/null — we don't need MAME's stdout output.
        // This prevents buffer deadlock.
        process.standardOutput = FileHandle.nullDevice

        // Reset captured output
        lastStderrOutput = ""
        lastStdoutOutput = ""

        // Read stderr asynchronously in background to drain the pipe buffer.
        // Use a thread-safe accumulator (NSLock-protected) since readabilityHandler
        // and terminationHandler run on different threads.
        let stderrHandle = stderrPipe.fileHandleForReading
        let stderrLock = NSLock()
        var stderrData = Data()

        stderrHandle.readabilityHandler = { handle in
            let data = handle.availableData
            if data.isEmpty {
                handle.readabilityHandler = nil
                return
            }
            stderrLock.lock()
            stderrData.append(data)
            stderrLock.unlock()
            if let text = String(data: data, encoding: .utf8), !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty {
                print("[MAME stderr] \(text.trimmingCharacters(in: .newlines))")
            }
        }

        // Set up termination handler
        process.terminationHandler = { [weak self] proc in
            // Release security-scoped access now that the process has exited
            if didAccessBinary { binaryURL.stopAccessingSecurityScopedResource() }

            guard let self = self else { return }

            // Stop async reading and drain any remaining bytes
            stderrHandle.readabilityHandler = nil
            let remainingData = stderrHandle.readDataToEndOfFile()

            stderrLock.lock()
            stderrData.append(remainingData)
            let finalStderr = String(data: stderrData, encoding: .utf8) ?? ""
            stderrLock.unlock()

            self.lastStderrOutput = finalStderr
            let exitCode = proc.terminationStatus

            print("[MAME] Process exited with code: \(exitCode)")

            if exitCode != 0 {
                var msg = "MAME exited with code \(exitCode)."
                if !finalStderr.isEmpty {
                    let lines = finalStderr.components(separatedBy: .newlines)
                        .filter { !$0.isEmpty }
                        .prefix(8)
                    msg += "\n\n" + lines.joined(separator: "\n")
                }
                self.statusContinuation?.yield(.error(msg))
            } else {
                self.statusContinuation?.yield(.terminated(exitCode: exitCode))
            }
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

        // Set up file-based IPC: tell MAMELuaCommand where to write commands
        MAMELuaCommand.commandFilePath = cmdFilePath

        // Create session BEFORE activating MAME, because session setup
        // triggers SwiftUI updates that can steal focus from MAME.
        let session = await EmulationSession(
            machineProfile: machine,
            media: media,
            status: .running
        )
        await MainActor.run {
            session.processID = process.processIdentifier
        }

        // Activate MAME AFTER all session/UI setup is complete.
        // This ensures SwiftUI updates from session creation don't steal focus back.
        // EmuBuddy's NSStatusItem (system tray) remains visible for controls.
        let mamePID = process.processIdentifier
        print("[EmuBuddy] MAME launched (PID: \(mamePID)). Will activate after session setup.")

        Task.detached {
            // Wait a bit longer to let SwiftUI settle after session assignment
            try? await Task.sleep(nanoseconds: 500_000_000) // 500ms initial delay
            for attempt in 1...20 {
                guard process.isRunning else {
                    print("[EmuBuddy] MAME exited before window appeared")
                    break
                }
                if let mameApp = NSRunningApplication(processIdentifier: mamePID) {
                    let ok = mameApp.activate(options: [.activateIgnoringOtherApps])
                    print("[EmuBuddy] MAME activation attempt \(attempt): \(ok)")
                    if ok { break }
                }
                try? await Task.sleep(nanoseconds: 250_000_000) // 250ms between attempts
            }
        }

        return session
    }

    // MARK: - Terminate

    func terminate(session: EmulationSession) async {
        // Try graceful exit first via Lua command
        MAMELuaCommand.exit()

        // Give MAME a moment to shut down gracefully
        try? await Task.sleep(nanoseconds: 500_000_000) // 500ms

        // Force-terminate if still running
        if activeProcess?.isRunning == true {
            activeProcess?.terminate()
        }
        activeProcess = nil

        // Clear the command file path
        MAMELuaCommand.commandFilePath = nil

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

    // MARK: - Plugin Setup

    /// Ensures the EmuBuddy Lua plugin files exist on disk for MAME to load.
    /// Returns the base directory containing the `emubuddy/` plugin folder.
    private func ensurePluginFiles() -> String {
        // First check if plugins are bundled in the app
        if let bundledPlugins = Bundle.main.resourceURL?.appendingPathComponent("plugins"),
           FileManager.default.fileExists(atPath: bundledPlugins.appendingPathComponent("emubuddy/init.lua").path) {
            let path = bundledPlugins.path
            print("[EmuBuddy] Using bundled plugins at: \(path)")
            return path
        }

        // Otherwise, write them to Application Support
        let appSupport = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
        let pluginBase = appSupport.appendingPathComponent("EmuBuddy/plugins")
        let pluginDir = pluginBase.appendingPathComponent("emubuddy")

        let fm = FileManager.default
        try? fm.createDirectory(at: pluginDir, withIntermediateDirectories: true)

        // plugin.json
        let pluginJSON = """
        {
            "plugin": {
                "name": "emubuddy",
                "description": "EmuBuddy remote control via command file",
                "version": "1.0.0",
                "author": "EmuBuddy",
                "type": "plugin",
                "start": "false"
            }
        }
        """
        try? pluginJSON.write(to: pluginDir.appendingPathComponent("plugin.json"), atomically: true, encoding: .utf8)

        // init.lua
        let initLua = """
        -- EmuBuddy Remote Control Plugin
        -- Reads Lua commands from a file and executes them each frame.

        local exports = {}

        exports.name = "emubuddy"
        exports.version = "1.0.0"
        exports.description = "EmuBuddy remote control via command file"
        exports.license = "MIT"
        exports.author = { name = "EmuBuddy" }

        function exports.startplugin()
            local cmd_file = os.getenv("EMUBUDDY_CMD_FILE")
            if not cmd_file then
                print("[emubuddy plugin] EMUBUDDY_CMD_FILE not set, plugin disabled")
                return
            end

            print("[emubuddy plugin] Watching command file: " .. cmd_file)

            emu.register_periodic(function()
                local f = io.open(cmd_file, "r")
                if f then
                    local cmd = f:read("*all")
                    f:close()
                    os.remove(cmd_file)
                    if cmd and cmd ~= "" then
                        for line in cmd:gmatch("[^\\r\\n]+") do
                            local fn, err = load(line)
                            if fn then
                                local ok, result = pcall(fn)
                                if ok then
                                    print("[emubuddy plugin] OK: " .. line)
                                else
                                    print("[emubuddy plugin] Error: " .. line .. " -> " .. tostring(result))
                                end
                            else
                                print("[emubuddy plugin] Parse error: " .. line .. " -> " .. tostring(err))
                            end
                        end
                    end
                end
            end)
        end

        return exports
        """
        try? initLua.write(to: pluginDir.appendingPathComponent("init.lua"), atomically: true, encoding: .utf8)

        print("[EmuBuddy] Wrote plugin files to: \(pluginBase.path)")
        return pluginBase.path
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

        // Game I/O device (joystick, paddles, etc.)
        if let gameIO = machine.gameIODevice {
            args.append(contentsOf: ["-gameio", gameIO.mameDevice])
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

        // Enable the EmuBuddy Lua plugin for runtime control via file IPC.
        // Include MAME's own plugins dir (if it exists) so built-in plugins still work.
        let emubuddyPluginDir = ensurePluginFiles()
        pluginBaseDir = emubuddyPluginDir
        let mamePluginsDir = config.mameBinaryURL.deletingLastPathComponent()
            .appendingPathComponent("plugins").path
        let combinedPluginPath = emubuddyPluginDir + ";" + mamePluginsDir
        args.append(contentsOf: ["-pluginspath", combinedPluginPath])
        args.append(contentsOf: ["-plugin", "emubuddy"])

        // Display filter
        if machine.displaySettings.filter != .sharp {
            let mameDir = config.mameBinaryURL.deletingLastPathComponent().path
            args.append(contentsOf: [
                "-video", "bgfx",
                "-bgfx_path", mameDir + "/bgfx",
                "-artpath", mameDir + "/artwork",
                "-bgfx_screen_chains", machine.displaySettings.filter.mameBGFXChain
            ])
        }

        return args
    }

}
