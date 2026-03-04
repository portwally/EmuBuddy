import Foundation
import Combine

/// Manages save states — listing, creating, deleting, and loading.
@MainActor
final class SaveStateManager: ObservableObject {

    @Published var saveStates: [SaveState] = []

    private let configStore: ConfigStore

    init(configStore: ConfigStore) {
        self.configStore = configStore
    }

    /// Load all saved states from disk.
    func loadAll() {
        let url = configStore.saveStatesURL.appendingPathComponent("index.json")
        guard let data = try? Data(contentsOf: url),
              let states = try? JSONDecoder().decode([SaveState].self, from: data) else {
            saveStates = []
            return
        }
        saveStates = states.sorted { $0.timestamp > $1.timestamp }
    }

    /// Save the index to disk.
    func persist() {
        let url = configStore.saveStatesURL.appendingPathComponent("index.json")
        try? FileManager.default.createDirectory(at: configStore.saveStatesURL, withIntermediateDirectories: true)
        let data = try? JSONEncoder().encode(saveStates)
        try? data?.write(to: url)
    }

    /// Delete a save state and its associated files.
    func delete(_ state: SaveState) {
        saveStates.removeAll { $0.id == state.id }
        try? FileManager.default.removeItem(atPath: state.mameStateFilePath)
        if let thumbPath = state.thumbnailPath {
            try? FileManager.default.removeItem(atPath: thumbPath)
        }
        persist()
    }
}
