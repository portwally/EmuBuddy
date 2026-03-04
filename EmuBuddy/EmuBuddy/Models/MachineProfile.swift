import Foundation

/// A complete Apple II machine configuration.
struct MachineProfile: Codable, Identifiable, Hashable {
    let id: UUID
    var name: String
    var machineType: MachineType
    var romVersion: ROMVersion?
    var ramSize: RAMSize
    var cpuSpeed: CPUSpeed
    var slots: [Int: SlotCard]  // Slot 0–7
    var displaySettings: DisplaySettings
    var inputMapping: InputMapping

    init(
        id: UUID = UUID(),
        name: String,
        machineType: MachineType,
        romVersion: ROMVersion? = nil,
        ramSize: RAMSize = .kb128,
        cpuSpeed: CPUSpeed = .normal,
        slots: [Int: SlotCard] = [:],
        displaySettings: DisplaySettings = .default,
        inputMapping: InputMapping = .default
    ) {
        self.id = id
        self.name = name
        self.machineType = machineType
        self.romVersion = romVersion
        self.ramSize = ramSize
        self.cpuSpeed = cpuSpeed
        self.slots = slots
        self.displaySettings = displaySettings
        self.inputMapping = inputMapping
    }
}

// MARK: - Machine Type

enum MachineType: String, Codable, CaseIterable, Identifiable {
    case apple2       = "apple2"
    case apple2Plus   = "apple2p"
    case apple2e      = "apple2e"
    case apple2eEnhanced = "apple2ee"
    case apple2c      = "apple2c"
    case apple2gs     = "apple2gs"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .apple2:           return "Apple ]["
        case .apple2Plus:       return "Apple ][+"
        case .apple2e:          return "Apple //e"
        case .apple2eEnhanced:  return "Apple //e Enhanced"
        case .apple2c:          return "Apple //c"
        case .apple2gs:         return "Apple IIGS"
        }
    }

    /// The MAME driver name for this machine.
    var mameDriver: String { rawValue }

    var isIIGS: Bool { self == .apple2gs }
    var hasExpansionSlots: Bool { self != .apple2c }
    var slotCount: Int { isIIGS ? 8 : 8 }  // Slots 0–7
}

// MARK: - ROM Version

enum ROMVersion: String, Codable, CaseIterable {
    case rom01 = "rom01"
    case rom3  = "rom3"

    var displayName: String {
        switch self {
        case .rom01: return "ROM 01"
        case .rom3:  return "ROM 3"
        }
    }
}

// MARK: - RAM Size

enum RAMSize: String, Codable, CaseIterable {
    case kb48  = "48K"
    case kb64  = "64K"
    case kb128 = "128K"
    case mb1   = "1M"
    case mb2   = "2M"
    case mb4   = "4M"
    case mb8   = "8M"

    var displayName: String { rawValue }

    /// Valid RAM sizes for a given machine type.
    static func validSizes(for machine: MachineType) -> [RAMSize] {
        switch machine {
        case .apple2, .apple2Plus:
            return [.kb48, .kb64]
        case .apple2e, .apple2eEnhanced:
            return [.kb64, .kb128]
        case .apple2c:
            return [.kb128]
        case .apple2gs:
            return [.mb1, .mb2, .mb4, .mb8]
        }
    }

    /// MAME -ramsize argument value.
    var mameValue: String {
        switch self {
        case .kb48:  return "48K"
        case .kb64:  return "64K"
        case .kb128: return "128K"
        case .mb1:   return "1M"
        case .mb2:   return "2M"
        case .mb4:   return "4M"
        case .mb8:   return "8M"
        }
    }
}

// MARK: - CPU Speed

enum CPUSpeed: String, Codable, CaseIterable {
    case normal = "normal"
    case fast   = "fast"
    case warp   = "warp"

    var displayName: String {
        switch self {
        case .normal: return "Normal (1 MHz / 2.8 MHz)"
        case .fast:   return "Fast (2x)"
        case .warp:   return "Warp (Unlimited)"
        }
    }

    /// MAME -speed argument value.
    var mameSpeedValue: Double {
        switch self {
        case .normal: return 1.0
        case .fast:   return 2.0
        case .warp:   return 10.0
        }
    }
}

// MARK: - Slot Cards

enum SlotCard: String, Codable, CaseIterable, Identifiable {
    case diskII         = "diskii"
    case diskIIWoz      = "diskiiwoz"
    case smartPort      = "smartport"
    case mockingboardA  = "mockbdA"
    case mockingboardC  = "mockbdC"
    case superSerial    = "ssc"
    case parallel       = "parallel"
    case z80            = "z80"
    case mouse          = "mouse"
    case ramExpansion   = "ramcard"
    case col80          = "80col"
    case empty          = "empty"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .diskII:         return "Disk II"
        case .diskIIWoz:      return "Disk II (WOZ)"
        case .smartPort:      return "SmartPort"
        case .mockingboardA:  return "Mockingboard A"
        case .mockingboardC:  return "Mockingboard C"
        case .superSerial:    return "Super Serial Card"
        case .parallel:       return "Parallel Printer Card"
        case .z80:            return "Z-80 SoftCard"
        case .mouse:          return "Apple Mouse Card"
        case .ramExpansion:   return "RAM Expansion"
        case .col80:          return "80-Column Card"
        case .empty:          return "Empty"
        }
    }

    /// The MAME slot device name.
    var mameDevice: String { rawValue }
}

// MARK: - Presets

extension MachineProfile {
    static let presets: [MachineProfile] = [
        MachineProfile(
            name: "Apple ][+ (48K)",
            machineType: .apple2Plus,
            ramSize: .kb48,
            slots: [6: .diskII]
        ),
        MachineProfile(
            name: "Apple //e Enhanced",
            machineType: .apple2eEnhanced,
            ramSize: .kb128,
            slots: [3: .col80, 6: .diskII]
        ),
        MachineProfile(
            name: "Apple //c",
            machineType: .apple2c,
            ramSize: .kb128
        ),
        MachineProfile(
            name: "Apple IIGS (ROM 01, 1MB)",
            machineType: .apple2gs,
            romVersion: .rom01,
            ramSize: .mb1,
            slots: [5: .smartPort, 6: .diskIIWoz]
        ),
        MachineProfile(
            name: "Apple IIGS (ROM 3, 4MB)",
            machineType: .apple2gs,
            romVersion: .rom3,
            ramSize: .mb4,
            slots: [4: .mockingboardC, 5: .smartPort, 6: .diskIIWoz]
        ),
    ]
}
