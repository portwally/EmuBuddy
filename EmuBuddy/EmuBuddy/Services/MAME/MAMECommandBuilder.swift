import Foundation
import Combine

/// Utility for building and debugging MAME command lines.
struct MAMECommandBuilder {

    /// Builds a complete MAME command string for display/debugging.
    static func commandString(
        binary: URL,
        machine: MachineProfile,
        media: [MediaSlot: URL],
        config: MAMEConfig
    ) -> String {
        let engine = SubprocessMAMEEngine(config: ConfigStore())
        let args = engine.buildArguments(machine: machine, media: media, config: config)
        let escaped = args.map { arg in
            arg.contains(" ") ? "\"\(arg)\"" : arg
        }
        return ([binary.path] + escaped).joined(separator: " ")
    }

    /// Validates that required ROMs exist for a given machine type.
    static func validateROMs(
        for machine: MachineType,
        romPath: URL
    ) -> ROMValidationResult {
        let requiredROMs = requiredROMFiles(for: machine)
        let fileManager = FileManager.default

        var missing: [String] = []
        var found: [String] = []

        for rom in requiredROMs {
            let romDir = romPath.appendingPathComponent(machine.mameDriver)
            let romFile = romDir.appendingPathComponent(rom)
            if fileManager.fileExists(atPath: romFile.path) {
                found.append(rom)
            } else {
                missing.append(rom)
            }
        }

        return ROMValidationResult(
            machine: machine,
            found: found,
            missing: missing,
            isValid: missing.isEmpty
        )
    }

    /// Returns the list of ROM files MAME requires for a machine type.
    /// Note: These are approximate — MAME may require additional files depending on version.
    static func requiredROMFiles(for machine: MachineType) -> [String] {
        switch machine {
        case .apple2:
            return ["341-0001-00.e0", "341-0002-00.e8", "341-0003-00.f0",
                    "341-0004-00.f8", "341-0005-00.d0", "341-0006-00.d8"]
        case .apple2Plus:
            return ["341-0011.d0", "341-0012.d8", "341-0013.e0",
                    "341-0014.e8", "341-0015.f0", "341-0020.f8"]
        case .apple2e:
            return ["342-0135-b.64", "342-0134-a.64"]
        case .apple2eEnhanced:
            return ["342-0349-b.64", "342-0350-b.64"]
        case .apple2c:
            return ["342-0272-b.rom"]
        case .apple2gs:
            return ["341s0632-2.bin"]  // ROM 01; ROM 3 uses different file
        }
    }
}

struct ROMValidationResult {
    let machine: MachineType
    let found: [String]
    let missing: [String]
    let isValid: Bool
}
