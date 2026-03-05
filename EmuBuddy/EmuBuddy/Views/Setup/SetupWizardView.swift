import SwiftUI

/// First-run setup wizard: locate MAME binary, ROM directory, and disk image folders.
struct SetupWizardView: View {
    @EnvironmentObject var appState: AppState
    @Environment(\.dismiss) private var dismiss

    @State private var currentStep: SetupStep = .welcome
    @State private var mamePath: String = ""
    @State private var romPath: String = ""
    @State private var diskImagePaths: [URL] = []
    @State private var mameDetected: Bool = false
    @State private var romValidation: [MachineType: Bool] = [:]
    @State private var isValidating: Bool = false

    enum SetupStep: Int, CaseIterable {
        case welcome = 0
        case mameBinary = 1
        case romDirectory = 2
        case diskImages = 3
        case complete = 4
    }

    var body: some View {
        VStack(spacing: 0) {
            // Progress indicator
            HStack(spacing: 4) {
                ForEach(SetupStep.allCases, id: \.rawValue) { step in
                    RoundedRectangle(cornerRadius: 2)
                        .fill(step.rawValue <= currentStep.rawValue ? Color.accentColor : Color.secondary.opacity(0.3))
                        .frame(height: 4)
                }
            }
            .padding(.horizontal)
            .padding(.top)

            // Content
            Group {
                switch currentStep {
                case .welcome:
                    welcomeStep
                case .mameBinary:
                    mameBinaryStep
                case .romDirectory:
                    romDirectoryStep
                case .diskImages:
                    diskImagesStep
                case .complete:
                    completeStep
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Divider()

            // Navigation buttons
            HStack {
                if currentStep != .welcome {
                    Button("Back") {
                        withAnimation {
                            currentStep = SetupStep(rawValue: currentStep.rawValue - 1) ?? .welcome
                        }
                    }
                }

                Spacer()

                if currentStep == .complete {
                    Button("Get Started") {
                        saveSettings()
                        dismiss()
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                } else {
                    Button("Continue") {
                        withAnimation {
                            currentStep = SetupStep(rawValue: currentStep.rawValue + 1) ?? .complete
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(!canAdvance)
                }
            }
            .padding()
        }
        .frame(width: 600, height: 450)
        .onAppear {
            autoDetectMAME()
        }
    }

    var canAdvance: Bool {
        switch currentStep {
        case .welcome: return true
        case .mameBinary: return !mamePath.isEmpty && mameDetected
        case .romDirectory: return !romPath.isEmpty
        case .diskImages: return true  // optional
        case .complete: return true
        }
    }

    // MARK: - Steps

    var welcomeStep: some View {
        VStack(spacing: 20) {
            Image(systemName: "desktopcomputer")
                .font(.system(size: 64))
                .foregroundStyle(.tint)

            Text("Welcome to EmuBuddy")
                .font(.largeTitle)
                .fontWeight(.bold)

            Text("Let's set up your Apple II emulation environment.\nThis will only take a minute.")
                .multilineTextAlignment(.center)
                .foregroundStyle(.secondary)
        }
        .padding()
    }

    var mameBinaryStep: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Locate MAME Binary")
                .font(.title2)
                .fontWeight(.bold)

            Text("EmuBuddy uses a custom-built MAME for Apple II emulation. Point to the 'emubuddy' binary you built, or a standard MAME binary.")
                .foregroundStyle(.secondary)

            HStack {
                TextField("Path to MAME binary", text: $mamePath)
                    .textFieldStyle(.roundedBorder)
                    .onChange(of: mamePath) { _, _ in validateMAME() }

                Button("Browse...") {
                    browseForMAME()
                }
            }

            if mameDetected {
                Label("Binary found: ready to go", systemImage: "checkmark.circle.fill")
                    .foregroundStyle(.green)
            } else if !mamePath.isEmpty {
                Label("File not found at this path", systemImage: "xmark.circle.fill")
                    .foregroundStyle(.red)
            }
        }
        .padding(32)
    }

    var romDirectoryStep: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("ROM Directory")
                .font(.title2)
                .fontWeight(.bold)

            Text("MAME requires system ROM files to emulate Apple II hardware. Point to the directory containing your ROM files (e.g., apple2gs/, apple2ee/, etc.).")
                .foregroundStyle(.secondary)

            HStack {
                TextField("Path to ROM directory", text: $romPath)
                    .textFieldStyle(.roundedBorder)

                Button("Browse...") {
                    browseForROMs()
                }
            }

            if !romPath.isEmpty {
                Button("Validate ROMs") {
                    validateROMs()
                }
                .disabled(isValidating)

                if !romValidation.isEmpty {
                    VStack(alignment: .leading, spacing: 4) {
                        ForEach(Array(romValidation.keys).sorted(by: { $0.displayName < $1.displayName }), id: \.self) { machine in
                            HStack {
                                Image(systemName: romValidation[machine] == true ? "checkmark.circle.fill" : "xmark.circle")
                                    .foregroundStyle(romValidation[machine] == true ? .green : .orange)
                                Text(machine.displayName)
                                    .font(.callout)
                            }
                        }
                    }
                    .padding(.top, 4)
                }
            }
        }
        .padding(32)
    }

    var diskImagesStep: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Disk Image Folders")
                .font(.title2)
                .fontWeight(.bold)

            Text("Add folders containing your Apple II disk images (.dsk, .woz, .2mg, .po, .do, .nib, .hdv). You can always add more later in Settings.")
                .foregroundStyle(.secondary)

            List {
                ForEach(diskImagePaths, id: \.self) { url in
                    HStack {
                        Image(systemName: "folder.fill")
                            .foregroundStyle(.tint)
                        Text(url.path)
                            .lineLimit(1)
                            .truncationMode(.middle)
                        Spacer()
                        Button(action: {
                            diskImagePaths.removeAll { $0 == url }
                        }) {
                            Image(systemName: "minus.circle.fill")
                                .foregroundStyle(.red)
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .frame(minHeight: 100)

            Button("Add Folder...") {
                browseForDiskImages()
            }
        }
        .padding(32)
    }

    var completeStep: some View {
        VStack(spacing: 20) {
            Image(systemName: "checkmark.circle.fill")
                .font(.system(size: 64))
                .foregroundStyle(.green)

            Text("You're All Set!")
                .font(.largeTitle)
                .fontWeight(.bold)

            VStack(alignment: .leading, spacing: 8) {
                Label(mamePath, systemImage: "terminal.fill")
                    .lineLimit(1)
                    .truncationMode(.middle)
                Label(romPath, systemImage: "cpu")
                    .lineLimit(1)
                    .truncationMode(.middle)
                Label("\(diskImagePaths.count) disk image folder(s)", systemImage: "externaldrive.fill")
            }
            .font(.callout)
            .foregroundStyle(.secondary)
        }
        .padding()
    }

    // MARK: - Actions

    func autoDetectMAME() {
        if let detected = appState.configStore.mameBinaryURL {
            mamePath = detected.path
            mameDetected = true
        }
    }

    func validateMAME() {
        // Check file exists and is executable. We accept any valid executable —
        // our custom build is named "emubuddy", not "mame".
        let fm = FileManager.default
        guard fm.fileExists(atPath: mamePath) else {
            mameDetected = false
            return
        }
        // isExecutableFile can fail under App Sandbox, so also check POSIX permissions
        if fm.isExecutableFile(atPath: mamePath) {
            mameDetected = true
        } else if let attrs = try? fm.attributesOfItem(atPath: mamePath),
                  let perms = attrs[.posixPermissions] as? Int,
                  perms & 0o111 != 0 {
            mameDetected = true
        } else {
            // If we got here via NSOpenPanel the user explicitly chose it — trust them
            mameDetected = fm.fileExists(atPath: mamePath)
        }
    }

    func browseForMAME() {
        let panel = NSOpenPanel()
        panel.title = "Select MAME Binary"
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            mamePath = url.path
            validateMAME()
        }
    }

    func browseForROMs() {
        let panel = NSOpenPanel()
        panel.title = "Select ROM Directory"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url {
            romPath = url.path
        }
    }

    func browseForDiskImages() {
        let panel = NSOpenPanel()
        panel.title = "Select Disk Image Folder"
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        if panel.runModal() == .OK {
            for url in panel.urls where !diskImagePaths.contains(url) {
                diskImagePaths.append(url)
            }
        }
    }

    func validateROMs() {
        isValidating = true
        let romURL = URL(fileURLWithPath: romPath)
        let machinesToCheck: [MachineType] = [
            .apple2Plus, .apple2eEnhanced, .apple2c, .apple2gsROM01, .apple2gsROM03
        ]

        romValidation = [:]
        for machine in machinesToCheck {
            let result = MAMECommandBuilder.validateROMs(for: machine, romPath: romURL)
            romValidation[machine] = result.isValid
        }
        isValidating = false
    }

    func saveSettings() {
        appState.configStore.mameBinaryURL = URL(fileURLWithPath: mamePath)
        appState.configStore.romDirectoryURL = URL(fileURLWithPath: romPath)
        appState.configStore.diskImageDirectories = diskImagePaths
        appState.isMAMEConfigured = true
    }
}
