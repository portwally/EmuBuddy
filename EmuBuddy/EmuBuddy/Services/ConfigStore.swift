import Foundation
import Combine

/// Persists user preferences and paths via UserDefaults.
/// Uses security-scoped bookmarks for sandbox-safe file access.
final class ConfigStore: ObservableObject {

    private let defaults = UserDefaults.standard

    // MARK: - MAME Binary
    // Default: look for our custom-built `emubuddy` binary

    var mameBinaryURL: URL? {
        get {
            // Try security-scoped bookmark first
            if let url = resolveBookmark(forKey: "mameBinaryBookmark") { return url }
            // Fall back to stored URL
            if let stored = defaults.url(forKey: "mameBinaryURL") { return stored }
            // Auto-detect common locations (use fileExists as isExecutableFile can fail on macOS)
            let candidates = [
                "/usr/local/bin/emubuddy",
                "\(NSHomeDirectory())/Documents/EmuBuddy/mame-build/mame/emubuddy",
            ]
            for path in candidates {
                if FileManager.default.fileExists(atPath: path) {
                    print("[ConfigStore] Auto-detected MAME binary at: \(path)")
                    return URL(fileURLWithPath: path)
                }
            }
            print("[ConfigStore] mameBinaryURL: nil (no binary found)")
            return nil
        }
        set {
            defaults.set(newValue, forKey: "mameBinaryURL")
            if let url = newValue {
                saveBookmark(url: url, forKey: "mameBinaryBookmark")
            } else {
                defaults.removeObject(forKey: "mameBinaryBookmark")
            }
            objectWillChange.send()
        }
    }

    // MARK: - ROM Path

    var romDirectoryURL: URL? {
        get {
            if let url = resolveBookmark(forKey: "romDirectoryBookmark") { return url }
            return defaults.url(forKey: "romDirectoryURL")
        }
        set {
            defaults.set(newValue, forKey: "romDirectoryURL")
            if let url = newValue {
                saveBookmark(url: url, forKey: "romDirectoryBookmark")
            } else {
                defaults.removeObject(forKey: "romDirectoryBookmark")
            }
            objectWillChange.send()
        }
    }

    // MARK: - Disk Image Directories

    var diskImageDirectories: [URL] {
        get {
            // Try bookmarks first
            if let bookmarkURLs = resolveBookmarkArray(forKey: "diskImageBookmarks"), !bookmarkURLs.isEmpty {
                return bookmarkURLs
            }
            // Fall back to JSON-encoded URLs (no security scope — scan will try direct access)
            guard let data = defaults.data(forKey: "diskImageDirectories"),
                  let urls = try? JSONDecoder().decode([URL].self, from: data) else {
                return []
            }
            return urls
        }
        set {
            let data = try? JSONEncoder().encode(newValue)
            defaults.set(data, forKey: "diskImageDirectories")
            saveBookmarkArray(urls: newValue, forKey: "diskImageBookmarks")
            objectWillChange.send()
        }
    }

    /// Re-save disk image folder bookmarks from current settings.
    /// Call this after the user re-selects folders to refresh security-scoped access.
    func refreshDiskImageBookmarks() {
        let current = diskImageDirectories
        if !current.isEmpty {
            saveBookmarkArray(urls: current, forKey: "diskImageBookmarks")
        }
    }

    // MARK: - Application Support Paths

    var appSupportURL: URL {
        let url = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
            .appendingPathComponent("EmuBuddy")
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    var mameConfigURL: URL { appSupportURL.appendingPathComponent("mame") }
    var saveStatesURL: URL { appSupportURL.appendingPathComponent("SaveStates") }
    var profilesURL: URL { appSupportURL.appendingPathComponent("Profiles") }

    var cachesURL: URL {
        let url = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first!
            .appendingPathComponent("EmuBuddy")
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    var thumbnailsURL: URL { cachesURL.appendingPathComponent("Thumbnails") }

    /// Build a MAMEConfig from current settings.
    func mameConfig() -> MAMEConfig? {
        guard let binary = mameBinaryURL,
              let roms = romDirectoryURL else {
            return nil
        }

        return MAMEConfig(
            mameBinaryURL: binary,
            romPath: roms,
            cfgPath: mameConfigURL.appendingPathComponent("cfg"),
            nvramPath: mameConfigURL.appendingPathComponent("nvram"),
            statePath: saveStatesURL,
            snapshotPath: mameConfigURL.appendingPathComponent("snap")
        )
    }

    // MARK: - Machine Profiles

    func savedProfiles() -> [MachineProfile] {
        let url = profilesURL.appendingPathComponent("profiles.json")
        print("[ConfigStore] Loading profiles from: \(url.path)")

        guard FileManager.default.fileExists(atPath: url.path) else {
            print("[ConfigStore] No profiles.json found, returning presets and saving them")
            let presets = MachineProfile.presets
            saveProfiles(presets)
            return presets
        }

        do {
            let data = try Data(contentsOf: url)
            var profiles = try JSONDecoder().decode([MachineProfile].self, from: data)
            print("[ConfigStore] Loaded \(profiles.count) profiles from disk")

            // Migrate: set default gameIODevice for profiles saved before this field existed
            var needsResave = false
            for i in profiles.indices {
                if profiles[i].gameIODevice == nil {
                    profiles[i].gameIODevice = .joystick
                    needsResave = true
                }
            }
            if needsResave {
                print("[ConfigStore] Migrating profiles: adding default gameIODevice=joystick")
                saveProfiles(profiles)
            }

            for p in profiles {
                let slots = p.slots.sorted(by: { $0.key < $1.key })
                    .map { "sl\($0.key)=\($0.value.displayName)" }
                    .joined(separator: ", ")
                print("[ConfigStore]   • \(p.name) [id=\(p.id.uuidString.prefix(8))...] gameIO=\(p.gameIODevice?.displayName ?? "none") slots=[\(slots)]")
            }
            return profiles
        } catch {
            print("[ConfigStore] ERROR decoding profiles.json: \(error)")
            print("[ConfigStore] Falling back to presets")
            return MachineProfile.presets
        }
    }

    func saveProfiles(_ profiles: [MachineProfile]) {
        let url = profilesURL.appendingPathComponent("profiles.json")
        print("[ConfigStore] Saving \(profiles.count) profiles to: \(url.path)")

        do {
            try FileManager.default.createDirectory(at: profilesURL, withIntermediateDirectories: true)
            let encoder = JSONEncoder()
            encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            let data = try encoder.encode(profiles)
            try data.write(to: url, options: .atomic)
            print("[ConfigStore] Profiles saved successfully (\(data.count) bytes)")
            for p in profiles {
                let slots = p.slots.sorted(by: { $0.key < $1.key })
                    .map { "sl\($0.key)=\($0.value.displayName)" }
                    .joined(separator: ", ")
                print("[ConfigStore]   • \(p.name) [id=\(p.id.uuidString.prefix(8))...] gameIO=\(p.gameIODevice?.displayName ?? "none") slots=[\(slots)]")
            }
        } catch {
            print("[ConfigStore] ERROR saving profiles: \(error)")
        }
    }

    // MARK: - Last Used Profile

    /// The UUID of the most recently used machine profile, persisted across launches.
    var lastUsedProfileID: UUID? {
        get {
            guard let str = defaults.string(forKey: "lastUsedProfileID") else { return nil }
            return UUID(uuidString: str)
        }
        set {
            defaults.set(newValue?.uuidString, forKey: "lastUsedProfileID")
        }
    }

    // MARK: - Security-Scoped Bookmarks

    /// Save a security-scoped bookmark for a URL.
    private func saveBookmark(url: URL, forKey key: String) {
        do {
            let bookmarkData = try url.bookmarkData(
                options: .withSecurityScope,
                includingResourceValuesForKeys: nil,
                relativeTo: nil
            )
            defaults.set(bookmarkData, forKey: key)
        } catch {
            print("[ConfigStore] Failed to save bookmark for \(key): \(error)")
        }
    }

    /// Resolve a security-scoped bookmark back to a URL.
    private func resolveBookmark(forKey key: String) -> URL? {
        guard let data = defaults.data(forKey: key) else { return nil }
        do {
            var isStale = false
            let url = try URL(resolvingBookmarkData: data,
                              options: .withSecurityScope,
                              relativeTo: nil,
                              bookmarkDataIsStale: &isStale)
            if isStale {
                // Re-save the bookmark
                saveBookmark(url: url, forKey: key)
            }
            // Start accessing the security-scoped resource
            _ = url.startAccessingSecurityScopedResource()
            return url
        } catch {
            print("[ConfigStore] Failed to resolve bookmark for \(key): \(error)")
            return nil
        }
    }

    /// Save an array of security-scoped bookmarks.
    private func saveBookmarkArray(urls: [URL], forKey key: String) {
        let bookmarks: [Data] = urls.compactMap { url in
            try? url.bookmarkData(
                options: .withSecurityScope,
                includingResourceValuesForKeys: nil,
                relativeTo: nil
            )
        }
        defaults.set(bookmarks, forKey: key)
    }

    /// Resolve an array of security-scoped bookmarks.
    private func resolveBookmarkArray(forKey key: String) -> [URL]? {
        guard let bookmarks = defaults.array(forKey: key) as? [Data] else { return nil }
        return bookmarks.compactMap { data in
            var isStale = false
            guard let url = try? URL(resolvingBookmarkData: data,
                                     options: .withSecurityScope,
                                     relativeTo: nil,
                                     bookmarkDataIsStale: &isStale) else { return nil }
            _ = url.startAccessingSecurityScopedResource()
            return url
        }
    }
}
