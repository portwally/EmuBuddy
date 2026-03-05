import Foundation
import Combine

/// Persists user preferences and paths via UserDefaults.
final class ConfigStore: ObservableObject {

    private let defaults = UserDefaults.standard

    // MARK: - MAME Binary
    // Default: look for our custom-built `emubuddy` binary

    var mameBinaryURL: URL? {
        get {
            if let stored = defaults.url(forKey: "mameBinaryURL") { return stored }
            // Auto-detect common locations
            let candidates = [
                "/usr/local/bin/emubuddy",
                "\(NSHomeDirectory())/Documents/EmuBuddy/mame-build/mame/emubuddy",
            ]
            for path in candidates {
                if FileManager.default.isExecutableFile(atPath: path) {
                    return URL(fileURLWithPath: path)
                }
            }
            return nil
        }
        set { defaults.set(newValue, forKey: "mameBinaryURL"); objectWillChange.send() }
    }

    // MARK: - ROM Path

    var romDirectoryURL: URL? {
        get { defaults.url(forKey: "romDirectoryURL") }
        set { defaults.set(newValue, forKey: "romDirectoryURL"); objectWillChange.send() }
    }

    // MARK: - Disk Image Directories

    var diskImageDirectories: [URL] {
        get {
            guard let data = defaults.data(forKey: "diskImageDirectories"),
                  let urls = try? JSONDecoder().decode([URL].self, from: data) else {
                return []
            }
            return urls
        }
        set {
            let data = try? JSONEncoder().encode(newValue)
            defaults.set(data, forKey: "diskImageDirectories")
            objectWillChange.send()
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
        guard let data = try? Data(contentsOf: url),
              let profiles = try? JSONDecoder().decode([MachineProfile].self, from: data) else {
            return MachineProfile.presets
        }
        return profiles
    }

    func saveProfiles(_ profiles: [MachineProfile]) {
        let url = profilesURL.appendingPathComponent("profiles.json")
        try? FileManager.default.createDirectory(at: profilesURL, withIntermediateDirectories: true)
        let data = try? JSONEncoder().encode(profiles)
        try? data?.write(to: url)
    }
}
