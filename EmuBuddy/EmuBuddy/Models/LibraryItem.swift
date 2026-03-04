import Foundation

/// A disk image or media file in the user's library.
struct LibraryItem: Identifiable, Hashable {
    let id: UUID
    var url: URL
    var mediaType: MediaType
    var title: String
    var publisher: String?
    var year: Int?
    var genre: Genre?
    var machineCompatibility: Set<MachineType>
    var copyProtected: Bool
    var bootSystem: BootSystem?
    var artworkURL: URL?
    var lastPlayed: Date?
    var playCount: Int
    var isFavorite: Bool
    var tags: Set<String>
    var fileSize: Int64
    var dateAdded: Date

    init(
        id: UUID = UUID(),
        url: URL,
        mediaType: MediaType,
        title: String? = nil,
        fileSize: Int64 = 0
    ) {
        self.id = id
        self.url = url
        self.mediaType = mediaType
        self.title = title ?? url.deletingPathExtension().lastPathComponent
        self.machineCompatibility = []
        self.copyProtected = false
        self.playCount = 0
        self.isFavorite = false
        self.tags = []
        self.fileSize = fileSize
        self.dateAdded = Date()
    }
}

// MARK: - Media Type

enum MediaType: String, Codable, CaseIterable, Identifiable {
    case dsk  = "dsk"   // Raw sector dump (140K or 800K)
    case do_  = "do"    // DOS-ordered sector dump
    case po   = "po"    // ProDOS-ordered sector dump
    case img2 = "2mg"   // 2IMG container format
    case woz  = "woz"   // WOZ 1.0/2.0 flux-level
    case nib  = "nib"   // Nibblized format
    case hdv  = "hdv"   // Hard disk volume

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .dsk:  return "DSK (Raw Sector)"
        case .do_:  return "DO (DOS-Ordered)"
        case .po:   return "PO (ProDOS-Ordered)"
        case .img2: return "2MG (2IMG Container)"
        case .woz:  return "WOZ (Flux-Level)"
        case .nib:  return "NIB (Nibblized)"
        case .hdv:  return "HDV (Hard Disk)"
        }
    }

    var fileExtension: String { rawValue }

    /// The MAME media flag for this type.
    var mameMediaFlag: String {
        switch self {
        case .hdv: return "-hard1"
        default:   return "-flop1"
        }
    }

    /// All recognized file extensions for disk images.
    static var allExtensions: Set<String> {
        Set(allCases.map(\.rawValue) + ["2mg"])
    }

    /// Initialize from a file extension string.
    static func from(extension ext: String) -> MediaType? {
        let lower = ext.lowercased()
        if lower == "2mg" { return .img2 }
        return MediaType(rawValue: lower)
    }
}

// MARK: - Genre

enum Genre: String, Codable, CaseIterable {
    case adventure    = "Adventure"
    case arcade       = "Arcade"
    case educational  = "Educational"
    case puzzle       = "Puzzle"
    case rpg          = "RPG"
    case simulation   = "Simulation"
    case sports       = "Sports"
    case strategy     = "Strategy"
    case utility      = "Utility"
    case productivity = "Productivity"
    case demo         = "Demo"
    case other        = "Other"
}

// MARK: - Boot System

enum BootSystem: String, Codable, CaseIterable {
    case dos33    = "DOS 3.3"
    case prodos8  = "ProDOS 8"
    case gsOS     = "GS/OS"
    case pascal   = "Apple Pascal"
    case cpm      = "CP/M"
    case unknown  = "Unknown"

    var displayName: String { rawValue }
}

// MARK: - Media Slot

enum MediaSlot: String, Codable, Hashable {
    case floppy1 = "flop1"
    case floppy2 = "flop2"
    case hard1   = "hard1"
    case hard2   = "hard2"

    var displayName: String {
        switch self {
        case .floppy1: return "Disk 1"
        case .floppy2: return "Disk 2"
        case .hard1:   return "Hard Drive 1"
        case .hard2:   return "Hard Drive 2"
        }
    }

    var mameFlag: String { "-\(rawValue)" }
}
