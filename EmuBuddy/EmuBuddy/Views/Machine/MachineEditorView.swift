import SwiftUI

/// Full machine profile editor: name, type, RAM, CPU speed, and visual slot configurator.
struct MachineEditorView: View {
    @EnvironmentObject var appState: AppState
    @Environment(\.dismiss) private var dismiss

    @State var profile: MachineProfile
    let isNew: Bool
    let onSave: (MachineProfile) -> Void

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text(isNew ? "New Machine Profile" : "Edit Profile")
                    .font(.title2)
                    .fontWeight(.bold)
                Spacer()
            }
            .padding()

            Divider()

            HSplitView {
                // Left: Configuration
                Form {
                    Section("General") {
                        TextField("Profile Name", text: $profile.name)

                        Picker("Machine Type", selection: $profile.machineType) {
                            ForEach(MachineFamily.allCases, id: \.self) { family in
                                Section(family.rawValue) {
                                    ForEach(MachineType.allCases.filter { $0.family == family }) { type in
                                        Text(type.displayName).tag(type)
                                    }
                                }
                            }
                        }

                        Picker("RAM", selection: $profile.ramSize) {
                            ForEach(RAMSize.validSizes(for: profile.machineType), id: \.self) { size in
                                Text(size.displayName).tag(size)
                            }
                        }

                        Picker("CPU Speed", selection: $profile.cpuSpeed) {
                            ForEach(CPUSpeed.allCases) { speed in
                                Text(speed.displayName).tag(speed)
                            }
                        }
                    }

                    Section("Input") {
                        Picker("Game I/O Device", selection: $profile.gameIODevice) {
                            Text("None").tag(GameIODevice?.none)
                            ForEach(GameIODevice.allCases) { device in
                                Text(device.displayName).tag(GameIODevice?.some(device))
                            }
                        }

                        Picker("Joystick Source", selection: $profile.inputMapping.joystickSource) {
                            ForEach(JoystickSource.allCases, id: \.self) { source in
                                Text(source.displayName).tag(source)
                            }
                        }
                    }

                    Section("Display") {
                        Picker("Filter", selection: $profile.displaySettings.filter) {
                            ForEach(DisplayFilter.allCases, id: \.self) { filter in
                                Text(filter.displayName).tag(filter)
                            }
                        }

                        Picker("Aspect Ratio", selection: $profile.displaySettings.aspectRatio) {
                            ForEach(AspectRatio.allCases, id: \.self) { ratio in
                                Text(ratio.displayName).tag(ratio)
                            }
                        }

                        Picker("Window Mode", selection: $profile.displaySettings.windowMode) {
                            ForEach(WindowMode.allCases, id: \.self) { mode in
                                Text(mode.displayName).tag(mode)
                            }
                        }

                        Picker("Color", selection: $profile.displaySettings.colorMode) {
                            ForEach(ColorMode.allCases, id: \.self) { mode in
                                Text(mode.displayName).tag(mode)
                            }
                        }
                    }
                }
                .formStyle(.grouped)
                .frame(minWidth: 320)

                // Right: Slot Configurator
                if profile.machineType.hasExpansionSlots {
                    SlotConfiguratorView(
                        slots: $profile.slots,
                        configurableSlots: profile.machineType.configurableSlots
                    )
                    .frame(minWidth: 350)
                } else {
                    VStack {
                        Image(systemName: "rectangle.slash")
                            .font(.system(size: 40))
                            .foregroundStyle(.secondary)
                        Text("No Expansion Slots")
                            .font(.title3)
                            .foregroundStyle(.secondary)
                        Text("The \(profile.machineType.displayName) has no user-configurable expansion slots.")
                            .font(.callout)
                            .foregroundStyle(.tertiary)
                            .multilineTextAlignment(.center)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }

            Divider()

            // Footer
            HStack {
                Button("Cancel") { dismiss() }
                    .keyboardShortcut(.cancelAction)
                Spacer()
                Button("Save") {
                    onSave(profile)
                    dismiss()
                }
                .buttonStyle(.borderedProminent)
                .keyboardShortcut(.defaultAction)
                .disabled(profile.name.isEmpty)
            }
            .padding()
        }
        .frame(width: 860, height: 600)
        .onChange(of: profile.machineType) { _, newType in
            // Reset RAM to a valid value when machine type changes
            let valid = RAMSize.validSizes(for: newType)
            if !valid.contains(profile.ramSize) {
                profile.ramSize = valid.first ?? .kb64
            }
            // Clear slots for machines without expansion
            if !newType.hasExpansionSlots {
                profile.slots = [:]
            }
        }
    }
}

// MARK: - Visual Slot Configurator

struct SlotConfiguratorView: View {
    @Binding var slots: [Int: SlotCard]
    let configurableSlots: [Int]
    @State private var popoverSlot: Int?

    var body: some View {
        VStack(spacing: 0) {
            Text("Expansion Slots")
                .font(.headline)
                .padding(.top, 16)
                .padding(.bottom, 12)

            Text("Click a slot to assign a card")
                .font(.caption)
                .foregroundStyle(.secondary)
                .padding(.bottom, 12)

            // Slot overview (the "motherboard")
            VStack(spacing: 6) {
                ForEach(configurableSlots, id: \.self) { slotNum in
                    let card = slots[slotNum] ?? .empty
                    HStack(spacing: 8) {
                        Text("Slot \(slotNum)")
                            .font(.system(.callout, design: .monospaced))
                            .frame(width: 50, alignment: .trailing)

                        Button(action: {
                            popoverSlot = slotNum
                        }) {
                            HStack(spacing: 6) {
                                if card != .empty {
                                    Image(systemName: cardIcon(card.category))
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                                Text(card == .empty ? "Empty" : card.displayName)
                                    .font(.callout)
                                    .lineLimit(1)
                                Spacer()
                                if card != .empty {
                                    Text(card.category.rawValue)
                                        .font(.caption2)
                                        .foregroundStyle(.tertiary)
                                }
                                Image(systemName: "chevron.right")
                                    .font(.caption2)
                                    .foregroundStyle(.tertiary)
                            }
                            .padding(.horizontal, 10)
                            .padding(.vertical, 8)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(cardColor(card))
                            .clipShape(RoundedRectangle(cornerRadius: 6))
                        }
                        .buttonStyle(.plain)
                        .popover(isPresented: Binding(
                            get: { popoverSlot == slotNum },
                            set: { if !$0 { popoverSlot = nil } }
                        ), arrowEdge: .trailing) {
                            CardPickerPopover(
                                slotNumber: slotNum,
                                currentCard: slots[slotNum] ?? .empty,
                                onSelect: { card in
                                    slots[slotNum] = card
                                    popoverSlot = nil
                                }
                            )
                        }

                        Button(action: {
                            slots[slotNum] = .empty
                        }) {
                            Image(systemName: "xmark.circle.fill")
                                .foregroundStyle(.secondary)
                                .font(.body)
                        }
                        .buttonStyle(.plain)
                        .opacity(card == .empty ? 0 : 1)
                    }
                    .padding(.horizontal, 12)
                }
            }

            Spacer()
        }
    }

    private func cardColor(_ card: SlotCard) -> Color {
        if card == .empty { return Color.secondary.opacity(0.08) }
        switch card.category {
        case .diskStorage: return Color.blue.opacity(0.12)
        case .audio: return Color.purple.opacity(0.12)
        case .serialParallel: return Color.orange.opacity(0.12)
        case .memory: return Color.green.opacity(0.12)
        case .video: return Color.cyan.opacity(0.12)
        case .coprocessor: return Color.red.opacity(0.12)
        case .input: return Color.yellow.opacity(0.12)
        case .network: return Color.indigo.opacity(0.12)
        case .other: return Color.gray.opacity(0.12)
        }
    }

    private func cardIcon(_ category: SlotCardCategory) -> String {
        switch category {
        case .diskStorage: return "internaldrive"
        case .audio: return "speaker.wave.2"
        case .serialParallel: return "cable.connector"
        case .memory: return "memorychip"
        case .video: return "display"
        case .coprocessor: return "cpu"
        case .input: return "computermouse"
        case .network: return "network"
        case .other: return "puzzlepiece"
        }
    }
}

// MARK: - Card Picker Popover

struct CardPickerPopover: View {
    let slotNumber: Int
    let currentCard: SlotCard
    let onSelect: (SlotCard) -> Void

    @State private var searchText = ""
    @State private var selectedCategory: SlotCardCategory?

    private var filteredCards: [SlotCard] {
        let allCards = SlotCard.allCases.filter { $0 != .empty }
        var cards = allCards

        if let cat = selectedCategory {
            cards = cards.filter { $0.category == cat }
        }
        if !searchText.isEmpty {
            let query = searchText.lowercased()
            cards = cards.filter {
                $0.displayName.lowercased().contains(query) ||
                $0.rawValue.lowercased().contains(query)
            }
        }
        return cards
    }

    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text("Slot \(slotNumber)")
                    .font(.headline)
                Spacer()
                Button("Clear") {
                    onSelect(.empty)
                }
                .font(.caption)
                .disabled(currentCard == .empty)
            }
            .padding(.horizontal, 12)
            .padding(.top, 12)
            .padding(.bottom, 8)

            // Search
            TextField("Search cards...", text: $searchText)
                .textFieldStyle(.roundedBorder)
                .padding(.horizontal, 12)
                .padding(.bottom, 6)

            // Category filter chips
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 4) {
                    CategoryChip(title: "All", isSelected: selectedCategory == nil) {
                        selectedCategory = nil
                    }
                    ForEach(SlotCardCategory.allCases, id: \.self) { cat in
                        CategoryChip(title: shortCategoryName(cat), isSelected: selectedCategory == cat) {
                            selectedCategory = (selectedCategory == cat) ? nil : cat
                        }
                    }
                }
                .padding(.horizontal, 12)
            }
            .padding(.bottom, 6)

            Divider()

            // Card list
            List {
                if selectedCategory == nil && searchText.isEmpty {
                    // Show common cards first when no filter
                    Section("Common") {
                        ForEach(SlotCard.commonCards, id: \.self) { card in
                            CardRow(card: card, isCurrent: card == currentCard) {
                                onSelect(card)
                            }
                        }
                    }
                    Section("All Cards") {
                        ForEach(filteredCards.filter { !SlotCard.commonCards.contains($0) }, id: \.self) { card in
                            CardRow(card: card, isCurrent: card == currentCard) {
                                onSelect(card)
                            }
                        }
                    }
                } else {
                    ForEach(filteredCards, id: \.self) { card in
                        CardRow(card: card, isCurrent: card == currentCard) {
                            onSelect(card)
                        }
                    }
                }
            }
            .listStyle(.plain)
        }
        .frame(width: 340, height: 420)
    }

    private func shortCategoryName(_ cat: SlotCardCategory) -> String {
        switch cat {
        case .diskStorage: return "Storage"
        case .audio: return "Audio"
        case .serialParallel: return "Serial"
        case .memory: return "Memory"
        case .video: return "Video"
        case .coprocessor: return "CPU"
        case .input: return "Input"
        case .network: return "Network"
        case .other: return "Other"
        }
    }
}

struct CardRow: View {
    let card: SlotCard
    let isCurrent: Bool
    let onSelect: () -> Void

    var body: some View {
        Button(action: onSelect) {
            HStack(spacing: 8) {
                Image(systemName: cardIcon(card.category))
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .frame(width: 16)

                VStack(alignment: .leading, spacing: 1) {
                    Text(card.displayName)
                        .font(.callout)
                        .fontWeight(isCurrent ? .semibold : .regular)
                    Text(card.category.rawValue)
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }

                Spacer()

                if isCurrent {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundStyle(.tint)
                }
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
    }

    private func cardIcon(_ category: SlotCardCategory) -> String {
        switch category {
        case .diskStorage: return "internaldrive"
        case .audio: return "speaker.wave.2"
        case .serialParallel: return "cable.connector"
        case .memory: return "memorychip"
        case .video: return "display"
        case .coprocessor: return "cpu"
        case .input: return "computermouse"
        case .network: return "network"
        case .other: return "puzzlepiece"
        }
    }
}

struct CategoryChip: View {
    let title: String
    let isSelected: Bool
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.caption2)
                .padding(.horizontal, 8)
                .padding(.vertical, 3)
                .background(isSelected ? Color.accentColor : Color.secondary.opacity(0.15))
                .foregroundStyle(isSelected ? .white : .primary)
                .clipShape(Capsule())
        }
        .buttonStyle(.plain)
    }
}
