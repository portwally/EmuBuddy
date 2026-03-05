import AppKit
import Combine

/// A macOS status bar menu (top-right of menu bar) that provides emulation
/// controls when MAME is running. This is visible even when MAME is the
/// frontmost app, unlike EmuBuddy's own menu bar.
@MainActor
final class EmulationStatusBar {

    private var statusItem: NSStatusItem?
    private var cancellables = Set<AnyCancellable>()
    private weak var appState: AppState?
    private var hasSession = false

    init(appState: AppState) {
        self.appState = appState
        setupStatusItem()

        // Observe activeSession to enable/disable menu items and update the shared handle
        appState.$activeSession
            .receive(on: DispatchQueue.main)
            .sink { [weak self] session in
                let running = (session != nil)
                self?.hasSession = running
                // Update the shared target with current PID
                EmulationMenuTarget.shared.mamePID = session?.processID
                EmulationMenuTarget.shared.stopHandler = { [weak appState] in
                    Task { @MainActor in
                        await appState?.stopSession()
                    }
                }
                self?.rebuildMenu()
                print("[EmulationStatusBar] Session changed — running: \(running)")
            }
            .store(in: &cancellables)
    }

    // MARK: - Setup

    private func setupStatusItem() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        if let button = item.button {
            button.image = NSImage(systemSymbolName: "gamecontroller.fill", accessibilityDescription: "EmuBuddy")
            button.toolTip = "EmuBuddy"
        }
        statusItem = item
        rebuildMenu()
    }

    // MARK: - Menu

    private func rebuildMenu() {
        let menu = NSMenu(title: "EmuBuddy")

        if hasSession, let profile = appState?.activeSession?.machineProfile {
            let header = NSMenuItem(title: "Running: \(profile.name)", action: nil, keyEquivalent: "")
            header.isEnabled = false
            menu.addItem(header)
        } else {
            let header = NSMenuItem(title: "No Emulation Running", action: nil, keyEquivalent: "")
            header.isEnabled = false
            menu.addItem(header)
        }

        menu.addItem(NSMenuItem.separator())

        menu.addItem(makeItem("Pause / Resume", action: #selector(EmulationMenuTarget.pauseAction), key: "p"))
        menu.addItem(makeItem("Frame Advance", action: #selector(EmulationMenuTarget.frameAdvanceAction), key: "."))
        menu.addItem(makeItem("Toggle Throttle", action: #selector(EmulationMenuTarget.toggleThrottleAction), key: "t"))

        menu.addItem(NSMenuItem.separator())

        menu.addItem(makeItem("Soft Reset", action: #selector(EmulationMenuTarget.softResetAction), key: "r"))
        let hardReset = makeItem("Hard Reset", action: #selector(EmulationMenuTarget.hardResetAction), key: "r")
        hardReset.keyEquivalentModifierMask = [.command, .option, .shift]
        menu.addItem(hardReset)

        menu.addItem(NSMenuItem.separator())

        menu.addItem(makeItem("Quick Save", action: #selector(EmulationMenuTarget.quickSaveAction), key: "s"))
        menu.addItem(makeItem("Quick Load", action: #selector(EmulationMenuTarget.quickLoadAction), key: "l"))

        menu.addItem(NSMenuItem.separator())

        menu.addItem(makeItem("Screenshot", action: #selector(EmulationMenuTarget.screenshotAction), key: "c"))

        menu.addItem(NSMenuItem.separator())

        menu.addItem(makeItem("Stop Emulation", action: #selector(EmulationMenuTarget.stopAction), key: "q"))

        menu.addItem(NSMenuItem.separator())

        // Show EmuBuddy — always enabled
        let showApp = NSMenuItem(title: "Show EmuBuddy", action: #selector(EmulationMenuTarget.showAppAction), keyEquivalent: "")
        showApp.target = EmulationMenuTarget.shared
        menu.addItem(showApp)

        statusItem?.menu = menu
    }

    private func makeItem(_ title: String, action: Selector, key: String) -> NSMenuItem {
        let item = NSMenuItem(title: title, action: action, keyEquivalent: key)
        if !key.isEmpty {
            item.keyEquivalentModifierMask = [.command, .option]
        }
        item.target = EmulationMenuTarget.shared
        item.isEnabled = hasSession
        return item
    }
}

// MARK: - Menu Action Target

/// NSObject target for NSMenuItem actions.
/// Sends Lua commands to MAME via file-based IPC (MAMELuaCommand).
/// After each action, re-activates MAME so it stays frontmost.
final class EmulationMenuTarget: NSObject {
    static let shared = EmulationMenuTarget()

    /// MAME's process ID — used to re-activate MAME after menu actions.
    var mamePID: Int32?

    /// Closure to stop the emulation session.
    var stopHandler: (() -> Void)?

    // MARK: - Helpers

    /// Re-activate MAME's window after the status bar menu closes.
    /// Clicking the status bar menu can steal focus to EmuBuddy.
    private func reactivateMAME() {
        guard let pid = mamePID else { return }
        // Short delay lets the status bar menu fully dismiss first
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) {
            if let mameApp = NSRunningApplication(processIdentifier: pid) {
                mameApp.activate(options: [.activateIgnoringOtherApps])
            }
        }
    }

    // MARK: - Actions

    @objc func pauseAction() {
        MAMELuaCommand.togglePause()
        reactivateMAME()
    }

    @objc func frameAdvanceAction() {
        MAMELuaCommand.frameAdvance()
        reactivateMAME()
    }

    @objc func toggleThrottleAction() {
        MAMELuaCommand.toggleThrottle()
        reactivateMAME()
    }

    @objc func softResetAction() {
        MAMELuaCommand.softReset()
        reactivateMAME()
    }

    @objc func hardResetAction() {
        MAMELuaCommand.hardReset()
        reactivateMAME()
    }

    @objc func quickSaveAction() {
        MAMELuaCommand.saveState()
        reactivateMAME()
    }

    @objc func quickLoadAction() {
        MAMELuaCommand.loadState()
        reactivateMAME()
    }

    @objc func screenshotAction() {
        MAMELuaCommand.screenshot()
        reactivateMAME()
    }

    @objc func stopAction() {
        print("[EmulationMenuTarget] Stop emulation")
        stopHandler?()
    }

    @objc func showAppAction() {
        NSApp.activate(ignoringOtherApps: true)
    }
}
