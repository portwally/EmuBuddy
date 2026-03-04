import Foundation
import Combine

/// Manages the user's disk image library — scanning, cataloging, and querying.
@MainActor
final class LibraryService: ObservableObject {

    @Published var items: [LibraryItem] = []
    @Published var isScanning: Bool = false

    private let configStore: ConfigStore

    init(configStore: ConfigStore) {
        self.configStore = configStore
    }

    /// Scan all configured directories for disk images.
    func scanAll() async {
        isScanning = true
        defer { isScanning = false }

        var foundItems: [LibraryItem] = []

        for directory in configStore.diskImageDirectories {
            let scanned = await scanDirectory(directory)
            foundItems.append(contentsOf: scanned)
        }

        items = foundItems
    }

    /// Scan a single directory (recursively) for recognized disk image files.
    func scanDirectory(_ directory: URL) async -> [LibraryItem] {
        let fileManager = FileManager.default
        var results: [LibraryItem] = []

        guard let enumerator = fileManager.enumerator(
            at: directory,
            includingPropertiesForKeys: [.fileSizeKey, .isRegularFileKey],
            options: [.skipsHiddenFiles]
        ) else {
            return results
        }

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
}
