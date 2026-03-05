import Foundation

/// Sends Lua commands to MAME via a command file that the EmuBuddy plugin reads.
///
/// Instead of using stdin (which requires linenoise), this writes commands to a
/// temp file. The MAME-side `emubuddy` Lua plugin polls this file each frame,
/// executes any commands found, and deletes the file.
///
/// This approach has zero external dependencies and works with any MAME build.
enum MAMELuaCommand {

    /// Path to the command file for the current session.
    /// Set when a session starts, cleared when it ends.
    static var commandFilePath: String?

    /// Write a Lua command to the command file for the MAME plugin to pick up.
    static func send(_ command: String) {
        guard let path = commandFilePath else {
            print("[MAMELua] No command file path set — cannot send command")
            return
        }
        do {
            try command.write(toFile: path, atomically: true, encoding: .utf8)
            print("[MAMELua] Sent: \(command)")
        } catch {
            print("[MAMELua] Write failed: \(error)")
        }
    }

    // MARK: - Emulation Control

    /// Toggle pause state.
    static func togglePause() {
        send("if emu.paused() then emu.unpause() else emu.pause() end")
    }

    /// Pause emulation.
    static func pause() { send("emu.pause()") }

    /// Unpause emulation.
    static func unpause() { send("emu.unpause()") }

    /// Soft reset (equivalent to pressing Reset button on the machine).
    static func softReset() { send("manager.machine:soft_reset()") }

    /// Hard reset (power cycle — clears all RAM).
    static func hardReset() { send("manager.machine:hard_reset()") }

    /// Save state to a named slot.
    static func saveState(name: String = "quick") {
        send("manager.machine:save(\"\(name)\")")
    }

    /// Load state from a named slot.
    static func loadState(name: String = "quick") {
        send("manager.machine:load(\"\(name)\")")
    }

    /// Toggle throttle (run at full speed vs normal speed).
    static func toggleThrottle() {
        send("manager.machine.video.throttled = not manager.machine.video.throttled")
    }

    /// Take a screenshot (saves to snapshot directory).
    static func screenshot() { send("manager.machine.video:snapshot()") }

    /// Step one frame while paused.
    static func frameAdvance() { send("emu.step()") }

    /// Gracefully exit MAME.
    static func exit() { send("manager.machine:exit()") }
}
