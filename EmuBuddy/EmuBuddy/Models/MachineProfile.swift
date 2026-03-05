import Foundation

/// A complete Apple II machine configuration.
struct MachineProfile: Codable, Identifiable, Hashable {
    let id: UUID
    var name: String
    var machineType: MachineType
    var ramSize: RAMSize
    var cpuSpeed: CPUSpeed
    var slots: [Int: SlotCard]  // Slot 1–7 for most machines
    var gameIODevice: GameIODevice?  // Game I/O port (joystick, paddles, etc.)
    var displaySettings: DisplaySettings
    var inputMapping: InputMapping

    init(
        id: UUID = UUID(),
        name: String,
        machineType: MachineType,
        ramSize: RAMSize = .kb128,
        cpuSpeed: CPUSpeed = .normal,
        slots: [Int: SlotCard] = [:],
        gameIODevice: GameIODevice? = .joystick,
        displaySettings: DisplaySettings = .default,
        inputMapping: InputMapping = .default
    ) {
        self.id = id
        self.name = name
        self.machineType = machineType
        self.ramSize = ramSize
        self.cpuSpeed = cpuSpeed
        self.slots = slots
        self.gameIODevice = gameIODevice
        self.displaySettings = displaySettings
        self.inputMapping = inputMapping
    }
}

extension MachineProfile {
    /// Whether this profile has a hard drive controller card in one of its slots.
    var hasHardDriveController: Bool {
        slots.values.contains { $0.isHardDriveController }
    }

    /// Whether this profile's machine type supports the enhanced //e firmware
    /// needed by CFFA2 (65C02-based machines: apple2ee, apple2ep, //c, IIGS).
    var supportsEnhancedCards: Bool {
        switch machineType {
        case .apple2eEnhanced, .apple2ePlatinum,
             .apple2c, .apple2cPlus,
             .apple2gsROM00, .apple2gsROM01, .apple2gsROM03:
            return true
        default:
            return false
        }
    }
}

// MARK: - Machine Type
// Matches actual MAME driver names from `emubuddy -listfull`

enum MachineType: String, Codable, CaseIterable, Identifiable {
    // Apple ][
    case apple2       = "apple2"
    case apple2Plus   = "apple2p"
    case apple2jPlus  = "apple2jp"

    // Apple //e family
    case apple2e           = "apple2e"
    case apple2eEnhanced   = "apple2ee"
    case apple2ePlatinum   = "apple2ep"

    // Apple //c family
    case apple2c       = "apple2c"
    case apple2cPlus   = "apple2cp"

    // Apple IIGS family
    case apple2gsROM00 = "apple2gsr0"
    case apple2gsROM01 = "apple2gsr1"
    case apple2gsROM03 = "apple2gs"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .apple2:           return "Apple ]["
        case .apple2Plus:       return "Apple ][+"
        case .apple2jPlus:      return "Apple ][ J-Plus"
        case .apple2e:          return "Apple //e"
        case .apple2eEnhanced:  return "Apple //e Enhanced"
        case .apple2ePlatinum:  return "Apple //e Platinum"
        case .apple2c:          return "Apple //c"
        case .apple2cPlus:      return "Apple //c Plus"
        case .apple2gsROM00:    return "Apple IIGS (ROM 00)"
        case .apple2gsROM01:    return "Apple IIGS (ROM 01)"
        case .apple2gsROM03:    return "Apple IIGS (ROM 03)"
        }
    }

    /// The MAME driver name for this machine.
    var mameDriver: String { rawValue }

    var isIIGS: Bool {
        switch self {
        case .apple2gsROM00, .apple2gsROM01, .apple2gsROM03: return true
        default: return false
        }
    }

    var isIIc: Bool {
        switch self {
        case .apple2c, .apple2cPlus: return true
        default: return false
        }
    }

    /// //c has no user-accessible expansion slots
    var hasExpansionSlots: Bool { !isIIc }

    /// Slots available for user configuration
    var configurableSlots: [Int] {
        if isIIc { return [] }
        return [1, 2, 3, 4, 5, 6, 7]
    }

    /// Machine family grouping for UI
    var family: MachineFamily {
        switch self {
        case .apple2, .apple2Plus, .apple2jPlus: return .appleII
        case .apple2e, .apple2eEnhanced, .apple2ePlatinum: return .appleIIe
        case .apple2c, .apple2cPlus: return .appleIIc
        case .apple2gsROM00, .apple2gsROM01, .apple2gsROM03: return .appleIIGS
        }
    }
}

enum MachineFamily: String, CaseIterable {
    case appleII  = "Apple ]["
    case appleIIe = "Apple //e"
    case appleIIc = "Apple //c"
    case appleIIGS = "Apple IIGS"
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
        case .apple2, .apple2Plus, .apple2jPlus:
            return [.kb48, .kb64]
        case .apple2e, .apple2eEnhanced, .apple2ePlatinum:
            return [.kb64]  // 128K comes from aux memory card, not -ramsize
        case .apple2c, .apple2cPlus:
            return [.kb128]
        case .apple2gsROM00, .apple2gsROM01, .apple2gsROM03:
            return [.mb1, .mb2, .mb4, .mb8]
        }
    }

    /// MAME -ramsize argument value.
    var mameValue: String { rawValue }
}

// MARK: - CPU Speed

enum CPUSpeed: String, Codable, CaseIterable, Identifiable {
    case normal = "normal"
    case fast   = "fast"
    case warp   = "warp"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .normal: return "Normal"
        case .fast:   return "Fast (2x)"
        case .warp:   return "Warp (Max)"
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
// All device names match `emubuddy apple2gs -listslots` output exactly.

enum SlotCardCategory: String, CaseIterable {
    case diskStorage    = "Disk & Storage"
    case audio          = "Audio & Sound"
    case serialParallel = "Serial & Parallel I/O"
    case memory         = "Memory Expansion"
    case video          = "Video & Display"
    case coprocessor    = "Coprocessor & Accelerator"
    case input          = "Input Devices"
    case network        = "Network & Clock"
    case other          = "Other"
}

enum SlotCard: String, Codable, CaseIterable, Identifiable {

    // ── Disk & Storage ────────────────────────────────────────
    case diskIIng       = "diskiing"       // Apple Disk II NG controller (16-sector)
    case superDrive     = "superdrive"     // Apple II 3.5" Disk Controller Card
    case cffa2          = "cffa2"          // CFFA 2.0 Compact Flash (65C02 firmware)
    case cffa202        = "cffa202"        // CFFA 2.0 Compact Flash (6502 firmware)
    case focusDrive     = "focusdrive"     // Parsons Engineering Focus Drive
    case zipDrive       = "zipdrive"       // Zip Technologies ZipDrive
    case pdRomDrive     = "pdromdrive"     // ProDOS ROM Drive
    case booti          = "booti"          // Booti Card
    case vulcan         = "vulcan"         // Applied Engineering Vulcan IDE (IIgs)
    case vulcanGold     = "vulcangold"     // Applied Engineering Vulcan Gold IDE (IIgs)

    // ── SCSI ──────────────────────────────────────────────────
    case scsi           = "scsi"           // Apple II SCSI Card
    case hsscsi         = "hsscsi"         // Apple II High-Speed SCSI Card
    case cmsscsi        = "cmsscsi"        // CMS SCSI II Card
    case corvus         = "corvus"         // Corvus Flat Cable interface
    case sider1         = "sider1"         // First Class Peripherals Sider 1 SASI Card
    case sider2         = "sider2"         // First Class Peripherals Sider 2 SASI Card

    // ── Audio & Sound ─────────────────────────────────────────
    case mockingboard   = "mockingboard"   // Sweet Micro Systems Mockingboard Sound/Speech I
    case phasor         = "phasor"         // Applied Engineering Phasor
    case aesms          = "aesms"          // Applied Engineering Super Music Synthesizer
    case alfam2         = "alfam2"         // ALF MC1 / Apple Music II
    case mcms1          = "mcms1"          // Mountain Computer Music System (card 1)
    case mcms2          = "mcms2"          // Mountain Computer Music System (card 2)
    case echoII         = "echoii"         // Street Electronics Echo II
    case echoIIPlus     = "echoiiplus"     // Street Electronics Echo Plus
    case sam            = "sam"            // Don't Ask Software S.A.M.
    case noisemaker     = "noisemaker"     // ADS Noisemaker II

    // ── Serial & Parallel I/O ─────────────────────────────────
    case ssc            = "ssc"            // Apple Super Serial Card
    case ssi            = "ssi"            // Apricorn Super Serial Imager
    case parallel       = "parallel"       // Apple II Parallel Interface Card
    case parprn         = "parprn"         // Apple II Parallel Printer Interface Card
    case grappler       = "grappler"       // Orange Micro Grappler Printer Interface
    case grapplus       = "grapplus"       // Orange Micro Grappler+ Printer Interface
    case bufgrapplus    = "bufgrapplus"    // Orange Micro Buffered Grappler+
    case bufgrapplusA   = "bufgrapplusa"   // Orange Micro Buffered Grappler+ (rev A)
    case fourDParPrn    = "4dparprn"       // Fourth Dimension Parallel Printer Interface
    case uniprint       = "uniprint"       // Videx Uniprint Printer Interface
    case midi           = "midi"           // 6850 MIDI card
    case ieee488        = "ieee488"        // Apple II IEEE-488 Interface
    case byte8251       = "byte8251"       // BYTE Serial Interface (8251 based)
    case ccs7710        = "ccs7710"        // CCS Model 7710 Asynchronous Serial Interface
    case ap2            = "ap2"            // IBS Computertechnik AP 2 Serial Interface

    // ── Memory Expansion ──────────────────────────────────────
    case memexp         = "memexp"         // Apple II Memory Expansion Card
    case ramfactor      = "ramfactor"      // Applied Engineering RamFactor

    // ── Video & Display ───────────────────────────────────────
    case videoterm      = "videoterm"      // Videx Videoterm 80 Column Display
    case vtc1           = "vtc1"           // Videoterm clone
    case ultraterm      = "ultraterm"      // Videx UltraTerm (original)
    case ultratermEnh   = "ultratermenh"   // Videx UltraTerm (enhanced //e)
    case aevm80         = "aevm80"         // Applied Engineering Viewmaster 80
    case ap16           = "ap16"           // IBS AP-16 80 column card
    case ap16alt        = "ap16alt"        // IBS AP-16 80 column card (alt)
    case ezcgi          = "ezcgi"          // E-Z Color Graphics Interface
    case ezcgi9938      = "ezcgi9938"      // E-Z Color Graphics Interface (TMS9938)
    case ezcgi9958      = "ezcgi9958"      // E-Z Color Graphics Interface (TMS9958)
    case grafex         = "grafex"         // Grafex-32

    // ── Coprocessor & Accelerator ─────────────────────────────
    case softcard       = "softcard"       // Microsoft SoftCard (Z80 CP/M)
    case themill        = "themill"        // Stellation Two The Mill (6809)
    case applicard      = "applicard"      // PCPI Applicard
    case q68            = "q68"            // Stellation Two Q-68 (68000)
    case q68plus        = "q68plus"        // Stellation Two Q-68 Plus

    // ── Input Devices ─────────────────────────────────────────
    case mouse          = "mouse"          // Apple II Mouse Card
    case fourPlay       = "4play"          // 4play Joystick Card (rev. B)
    case wicoTrackball  = "wicotrackball"  // Apple II Wico Trackball Card
    case dx1            = "dx1"            // Decillonix DX-1
    case snesmax        = "snesmax"        // SNES MAX Game Controller Interface

    // ── Network & Clock ───────────────────────────────────────
    case uthernet       = "uthernet"       // a2RetroSystems Uthernet
    case thclock        = "thclock"        // ThunderWare ThunderClock Plus
    case tm2ho          = "tm2ho"          // Applied Engineering TimeMaster H.O.

    // ── Other ─────────────────────────────────────────────────
    case arcbd          = "arcbd"          // Third Millenium Engineering Arcade Board
    case lancegs        = "lancegs"        // ///SHH Systeme LANceGS

    // ── Empty ─────────────────────────────────────────────────
    case empty          = ""

    var id: String { rawValue }

    /// The MAME -sl<n> device name.
    var mameDevice: String { rawValue }

    var displayName: String {
        switch self {
        // Disk & Storage
        case .diskIIng:       return "Disk II (16-sector)"
        case .superDrive:     return "3.5\" Disk Controller"
        case .cffa2:          return "CFFA 2.0 (65C02)"
        case .cffa202:        return "CFFA 2.0 (6502)"
        case .focusDrive:     return "Focus Drive"
        case .zipDrive:       return "Zip Drive"
        case .pdRomDrive:     return "ProDOS ROM Drive"
        case .booti:          return "Booti Card"
        case .vulcan:         return "Vulcan IDE"
        case .vulcanGold:     return "Vulcan Gold IDE"
        // SCSI
        case .scsi:           return "Apple SCSI"
        case .hsscsi:         return "High-Speed SCSI"
        case .cmsscsi:        return "CMS SCSI II"
        case .corvus:         return "Corvus"
        case .sider1:         return "Sider 1 SASI"
        case .sider2:         return "Sider 2 SASI"
        // Audio
        case .mockingboard:   return "Mockingboard"
        case .phasor:         return "Phasor"
        case .aesms:          return "Super Music Synthesizer"
        case .alfam2:         return "ALF Music II"
        case .mcms1:          return "Mountain Music System (1)"
        case .mcms2:          return "Mountain Music System (2)"
        case .echoII:         return "Echo II"
        case .echoIIPlus:     return "Echo Plus"
        case .sam:            return "S.A.M."
        case .noisemaker:     return "Noisemaker II"
        // Serial/Parallel
        case .ssc:            return "Super Serial Card"
        case .ssi:            return "Super Serial Imager"
        case .parallel:       return "Parallel Interface"
        case .parprn:         return "Parallel Printer"
        case .grappler:       return "Grappler"
        case .grapplus:       return "Grappler+"
        case .bufgrapplus:    return "Buffered Grappler+"
        case .bufgrapplusA:   return "Buffered Grappler+ (A)"
        case .fourDParPrn:    return "4th Dimension Parallel"
        case .uniprint:       return "Uniprint"
        case .midi:           return "MIDI"
        case .ieee488:        return "IEEE-488"
        case .byte8251:       return "BYTE Serial (8251)"
        case .ccs7710:        return "CCS 7710 Serial"
        case .ap2:            return "IBS AP 2 Serial"
        // Memory
        case .memexp:         return "Memory Expansion"
        case .ramfactor:      return "RamFactor"
        // Video
        case .videoterm:      return "Videoterm 80-Col"
        case .vtc1:           return "Videoterm Clone"
        case .ultraterm:      return "UltraTerm"
        case .ultratermEnh:   return "UltraTerm (Enhanced)"
        case .aevm80:         return "Viewmaster 80"
        case .ap16:           return "AP-16 80-Col"
        case .ap16alt:        return "AP-16 80-Col (Alt)"
        case .ezcgi:          return "E-Z Color Graphics"
        case .ezcgi9938:      return "E-Z Color (TMS9938)"
        case .ezcgi9958:      return "E-Z Color (TMS9958)"
        case .grafex:         return "Grafex-32"
        // Coprocessor
        case .softcard:       return "SoftCard (Z80)"
        case .themill:        return "The Mill (6809)"
        case .applicard:      return "Applicard"
        case .q68:            return "Q-68 (68000)"
        case .q68plus:        return "Q-68 Plus"
        // Input
        case .mouse:          return "Mouse Card"
        case .fourPlay:       return "4Play Joystick"
        case .wicoTrackball:  return "Wico Trackball"
        case .dx1:            return "DX-1"
        case .snesmax:        return "SNES MAX"
        // Network/Clock
        case .uthernet:       return "Uthernet"
        case .thclock:        return "ThunderClock Plus"
        case .tm2ho:          return "TimeMaster H.O."
        // Other
        case .arcbd:          return "Arcade Board"
        case .lancegs:        return "LANceGS"
        // Empty
        case .empty:          return "Empty"
        }
    }

    var category: SlotCardCategory {
        switch self {
        case .diskIIng, .superDrive, .cffa2, .cffa202, .focusDrive, .zipDrive,
             .pdRomDrive, .booti, .vulcan, .vulcanGold,
             .scsi, .hsscsi, .cmsscsi, .corvus, .sider1, .sider2:
            return .diskStorage
        case .mockingboard, .phasor, .aesms, .alfam2, .mcms1, .mcms2,
             .echoII, .echoIIPlus, .sam, .noisemaker:
            return .audio
        case .ssc, .ssi, .parallel, .parprn, .grappler, .grapplus,
             .bufgrapplus, .bufgrapplusA, .fourDParPrn, .uniprint,
             .midi, .ieee488, .byte8251, .ccs7710, .ap2:
            return .serialParallel
        case .memexp, .ramfactor:
            return .memory
        case .videoterm, .vtc1, .ultraterm, .ultratermEnh, .aevm80,
             .ap16, .ap16alt, .ezcgi, .ezcgi9938, .ezcgi9958, .grafex:
            return .video
        case .softcard, .themill, .applicard, .q68, .q68plus:
            return .coprocessor
        case .mouse, .fourPlay, .wicoTrackball, .dx1, .snesmax:
            return .input
        case .uthernet, .thclock, .tm2ho:
            return .network
        case .arcbd, .lancegs:
            return .other
        case .empty:
            return .other
        }
    }

    /// Cards commonly used — shown first in the slot configurator UI.
    static let commonCards: [SlotCard] = [
        .diskIIng, .superDrive, .mockingboard, .phasor, .ssc,
        .mouse, .memexp, .ramfactor, .cffa2, .softcard,
        .uthernet, .thclock, .vulcan, .hsscsi
    ]

    /// Whether this card provides hard drive media support (accepts -hard1, -hard2, etc.)
    var isHardDriveController: Bool {
        switch self {
        case .cffa2, .cffa202, .focusDrive, .zipDrive, .booti,
             .vulcan, .vulcanGold,
             .scsi, .hsscsi, .cmsscsi, .corvus, .sider1, .sider2:
            return true
        default:
            return false
        }
    }
}

// MARK: - Game I/O Devices (gameio slot)

enum GameIODevice: String, Codable, CaseIterable, Identifiable {
    case joystick    = "joy"
    case paddles     = "paddles"
    case compEyes    = "compeyes"
    case gizmo       = "gizmo"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .joystick:  return "Analog Joystick"
        case .paddles:   return "Paddles"
        case .compEyes:  return "ComputerEyes"
        case .gizmo:     return "HAL Labs Gizmo"
        }
    }

    var mameDevice: String { rawValue }
}

// MARK: - Presets

extension MachineProfile {
    static let presets: [MachineProfile] = [
        // Apple ][
        MachineProfile(
            name: "Apple ][+ (48K)",
            machineType: .apple2Plus,
            ramSize: .kb48,
            slots: [6: .diskIIng]
        ),
        // Apple //e (MAME apple2ee/apple2ep only support 64K base RAM)
        MachineProfile(
            name: "Apple //e Enhanced",
            machineType: .apple2eEnhanced,
            ramSize: .kb64,
            slots: [6: .diskIIng]
        ),
        MachineProfile(
            name: "Apple //e Platinum",
            machineType: .apple2ePlatinum,
            ramSize: .kb64,
            slots: [4: .mockingboard, 6: .diskIIng]
        ),
        // Apple //c (internal RAM, 128K valid)
        MachineProfile(
            name: "Apple //c",
            machineType: .apple2c,
            ramSize: .kb128
        ),
        MachineProfile(
            name: "Apple //c Plus",
            machineType: .apple2cPlus,
            ramSize: .kb128
        ),
        // Apple IIGS
        MachineProfile(
            name: "Apple IIGS (ROM 01, 1MB)",
            machineType: .apple2gsROM01,
            ramSize: .mb1,
            slots: [5: .diskIIng, 6: .superDrive]
        ),
        MachineProfile(
            name: "Apple IIGS (ROM 03, 4MB)",
            machineType: .apple2gsROM03,
            ramSize: .mb4,
            slots: [4: .mockingboard, 5: .diskIIng, 6: .superDrive, 7: .cffa2]
        ),
        MachineProfile(
            name: "Apple IIGS (ROM 03, 8MB, Networked)",
            machineType: .apple2gsROM03,
            ramSize: .mb8,
            slots: [1: .uthernet, 4: .mockingboard, 5: .diskIIng, 6: .superDrive, 7: .vulcanGold]
        ),
    ]
}
