import Foundation
import Combine

/// Manages the user's disk image library — scanning, cataloging, and querying.
@MainActor
final class LibraryService: ObservableObject {

    @Published var items: [LibraryItem] = []
    @Published var isScanning: Bool = false

    /// Tracks whether the initial scan has been performed this session.
    /// Lives here (not in a view's @State) so it persists across view recreation.
    private(set) var hasPerformedInitialScan = false

    private let configStore: ConfigStore
    private let metadataURL: URL

    init(configStore: ConfigStore) {
        self.configStore = configStore
        self.metadataURL = configStore.appSupportURL.appendingPathComponent("library-metadata.json")
        loadMetadata()
    }

    // MARK: - Scanning

    /// Perform the initial library scan (called once per app session).
    func scanIfNeeded() {
        guard !hasPerformedInitialScan, !isScanning else { return }
        guard !configStore.diskImageDirectories.isEmpty else { return }
        hasPerformedInitialScan = true
        // Use DispatchQueue.main.async to break out of the SwiftUI view update
        // cycle and avoid "Publishing changes from within view updates" warnings.
        DispatchQueue.main.async {
            Task { @MainActor in
                await self.scanAll()
            }
        }
    }

    /// Scan all configured directories for disk images.
    func scanAll() async {
        // Prevent concurrent scans
        guard !isScanning else {
            print("[LibraryService] Scan already in progress, skipping")
            return
        }

        isScanning = true

        let directories = configStore.diskImageDirectories
        print("[LibraryService] Starting scan of \(directories.count) director\(directories.count == 1 ? "y" : "ies")")
        for dir in directories {
            print("[LibraryService]   → \(dir.path)")
        }

        let existingMetadata = Dictionary(uniqueKeysWithValues: items.compactMap { item -> (URL, LibraryItem)? in
            (item.url, item)
        })
        var foundItems: [LibraryItem] = []

        for directory in directories {
            let scanned = await scanDirectory(directory)
            print("[LibraryService] Found \(scanned.count) items in \(directory.lastPathComponent)")
            foundItems.append(contentsOf: scanned)
        }

        print("[LibraryService] Total items found: \(foundItems.count)")

        // Merge: preserve play history, favorites, and tags from existing metadata
        let merged = foundItems.map { newItem in
            if let existing = existingMetadata[newItem.url] {
                var merged = newItem
                merged.lastPlayed = existing.lastPlayed
                merged.playCount = existing.playCount
                merged.isFavorite = existing.isFavorite
                merged.tags = existing.tags
                merged.publisher = existing.publisher
                merged.year = existing.year
                merged.genre = existing.genre
                merged.bootSystem = existing.bootSystem
                merged.machineCompatibility = existing.machineCompatibility
                return merged
            }
            return newItem
        }

        items = merged
        isScanning = false
    }

    /// Scan a single directory (recursively) for recognized disk image files.
    func scanDirectory(_ directory: URL) async -> [LibraryItem] {
        let fileManager = FileManager.default
        var results: [LibraryItem] = []

        // Start accessing security-scoped resource (needed for sandboxed apps)
        let didStartAccess = directory.startAccessingSecurityScopedResource()
        defer {
            if didStartAccess { directory.stopAccessingSecurityScopedResource() }
        }

        // Verify the directory is readable
        guard fileManager.isReadableFile(atPath: directory.path) else {
            print("[LibraryService] Cannot read directory: \(directory.path)")
            print("[LibraryService]   exists: \(fileManager.fileExists(atPath: directory.path))")
            print("[LibraryService]   securityScoped: \(didStartAccess)")
            return results
        }

        guard let enumerator = fileManager.enumerator(
            at: directory,
            includingPropertiesForKeys: [.fileSizeKey, .isRegularFileKey],
            options: [.skipsHiddenFiles]
        ) else {
            print("[LibraryService] Failed to create enumerator for: \(directory.path)")
            return results
        }

        let recognizedExtensions = MediaType.allExtensions
        print("[LibraryService] Scanning \(directory.path) for extensions: \(recognizedExtensions.sorted())")

        for case let fileURL as URL in enumerator {
            guard let resourceValues = try? fileURL.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey]),
                  resourceValues.isRegularFile == true else {
                continue
            }

            let ext = fileURL.pathExtension.lowercased()
            guard let mediaType = MediaType.from(extension: ext) else {
                continue
            }

            let item = LibraryItem(
                url: fileURL,
                mediaType: mediaType,
                fileSize: Int64(resourceValues.fileSize ?? 0)
            )
            results.append(item)
        }

        return results
    }

    // MARK: - Query

    /// Filter items by search text.
    func search(_ query: String) -> [LibraryItem] {
        guard !query.isEmpty else { return items }
        let lowered = query.lowercased()
        return items.filter { item in
            item.title.lowercased().contains(lowered) ||
            item.publisher?.lowercased().contains(lowered) == true ||
            item.tags.contains(where: { $0.lowercased().contains(lowered) })
        }
    }

    /// Filter items by machine compatibility.
    func items(for machineType: MachineType) -> [LibraryItem] {
        items.filter { $0.machineCompatibility.contains(machineType) || $0.machineCompatibility.isEmpty }
    }

    // MARK: - Mutations

    /// Record that a library item was played.
    func recordPlay(for url: URL) {
        if let idx = items.firstIndex(where: { $0.url == url }) {
            items[idx].lastPlayed = Date()
            items[idx].playCount += 1
            saveMetadata()
        }
    }

    /// Toggle favorite status for an item.
    func toggleFavorite(for item: LibraryItem) {
        if let idx = items.firstIndex(where: { $0.id == item.id }) {
            items[idx].isFavorite.toggle()
            saveMetadata()
        }
    }

    /// Update metadata for a library item.
    func updateMetadata(for item: LibraryItem, title: String? = nil, publisher: String? = nil,
                        year: Int? = nil, genre: Genre? = nil, tags: Set<String>? = nil) {
        if let idx = items.firstIndex(where: { $0.id == item.id }) {
            if let title = title { items[idx].title = title }
            if let publisher = publisher { items[idx].publisher = publisher }
            if let year = year { items[idx].year = year }
            if let genre = genre { items[idx].genre = genre }
            if let tags = tags { items[idx].tags = tags }
            saveMetadata()
        }
    }

    // MARK: - Persistence

    /// Library metadata we persist (play history, favorites, tags, etc.)
    private struct ItemMetadata: Codable {
        let urlPath: String
        var lastPlayed: Date?
        var playCount: Int
        var isFavorite: Bool
        var title: String?
        var publisher: String?
        var year: Int?
        var genre: Genre?
        var bootSystem: BootSystem?
        var tags: Set<String>
        var machineCompatibility: Set<String>
    }

    private func saveMetadata() {
        let metadata = items.filter { $0.playCount > 0 || $0.isFavorite || !$0.tags.isEmpty || $0.publisher != nil }
            .map { item -> ItemMetadata in
                ItemMetadata(
                    urlPath: item.url.path,
                    lastPlayed: item.lastPlayed,
                    playCount: item.playCount,
                    isFavorite: item.isFavorite,
                    title: item.title,
                    publisher: item.publisher,
                    year: item.year,
                    genre: item.genre,
                    bootSystem: item.bootSystem,
                    tags: item.tags,
                    machineCompatibility: Set(item.machineCompatibility.map(\.rawValue))
                )
            }

        do {
            let data = try JSONEncoder().encode(metadata)
            try data.write(to: metadataURL, options: .atomic)
        } catch {
            print("[LibraryService] Failed to save metadata: \(error)")
        }
    }

    private func loadMetadata() {
        guard let data = try? Data(contentsOf: metadataURL),
              let metadata = try? JSONDecoder().decode([ItemMetadata].self, from: data) else {
            return
        }

        // Store loaded metadata — will be merged during first scan
        for meta in metadata {
            let url = URL(fileURLWithPath: meta.urlPath)
            if let idx = items.firstIndex(where: { $0.url == url }) {
                items[idx].lastPlayed = meta.lastPlayed
                items[idx].playCount = meta.playCount
                items[idx].isFavorite = meta.isFavorite
                if let title = meta.title { items[idx].title = title }
                items[idx].publisher = meta.publisher
                items[idx].year = meta.year
                items[idx].genre = meta.genre
                items[idx].bootSystem = meta.bootSystem
                items[idx].tags = meta.tags
                items[idx].machineCompatibility = Set(meta.machineCompatibility.compactMap { MachineType(rawValue: $0) })
            }
        }
    }
}
