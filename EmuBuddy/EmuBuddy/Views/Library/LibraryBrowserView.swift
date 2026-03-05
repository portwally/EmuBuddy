import SwiftUI

/// Browse disk images in the library with search, filter, and launch.
struct LibraryBrowserView: View {
    @EnvironmentObject var appState: AppState
    @State private var searchText = ""
    @State private var viewMode: ViewMode = .grid
    @State private var selectedItemID: LibraryItem.ID?
    @State private var filterMediaType: MediaType?
    @State private var hasScanned = false

    /// Callback for quick-launch (picks default profile).
    var onLaunch: ((LibraryItem) -> Void)?
    /// Callback for "Launch with..." (shows profile picker sheet).
    var onLaunchWithOptions: ((LibraryItem) -> Void)?

    enum ViewMode {
        case list, grid
    }

    var filteredItems: [LibraryItem] {
        var results = appState.libraryService.search(searchText)
        if let filter = filterMediaType {
            results = results.filter { $0.mediaType == filter }
        }
        return results
    }

    private var selectedItem: LibraryItem? {
        filteredItems.first { $0.id == selectedItemID }
    }

    private func launchItem(_ item: LibraryItem) {
        print("[EmuBuddy] LibraryBrowserView.launchItem: \(item.title)")
        onLaunch?(item)
    }

    var body: some View {
        VStack(spacing: 0) {
            // Toolbar
            HStack {
                Text("Library")
                    .font(.title2)
                    .fontWeight(.semibold)

                if let item = selectedItem {
                    Button {
                        launchItem(item)
                    } label: {
                        Label("Play", systemImage: "play.fill")
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.regular)
                }

                Spacer()

                // Media type filter
                Picker("Type", selection: $filterMediaType) {
                    Text("All").tag(MediaType?.none)
                    ForEach(MediaType.allCases) { type in
                        Text(type.fileExtension.uppercased()).tag(MediaType?.some(type))
                    }
                }
                .frame(width: 100)

                Text("\(filteredItems.count) items")
                    .font(.callout)
                    .foregroundStyle(.secondary)

                Button(action: {
                    Task { await appState.libraryService.scanAll() }
                }) {
                    Image(systemName: "arrow.clockwise")
                }
                .help("Rescan Library")

                Picker("View", selection: $viewMode) {
                    Image(systemName: "list.bullet").tag(ViewMode.list)
                    Image(systemName: "square.grid.2x2").tag(ViewMode.grid)
                }
                .pickerStyle(.segmented)
                .frame(width: 80)
            }
            .padding()

            Divider()

            // Content
            if appState.libraryService.isScanning {
                VStack(spacing: 12) {
                    ProgressView()
                    Text("Scanning disk image folders...")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if filteredItems.isEmpty {
                EmptyLibraryView()
            } else {
                switch viewMode {
                case .list:
                    LibraryListView(
                        items: filteredItems,
                        selection: $selectedItemID,
                        onLaunch: launchItem,
                        onLaunchWithOptions: { item in onLaunchWithOptions?(item) },
                        onFavoriteToggle: { item in appState.libraryService.toggleFavorite(for: item) }
                    )
                case .grid:
                    LibraryGridView(
                        items: filteredItems,
                        selection: $selectedItemID,
                        onLaunch: launchItem,
                        onLaunchWithOptions: { item in onLaunchWithOptions?(item) },
                        onFavoriteToggle: { item in appState.libraryService.toggleFavorite(for: item) }
                    )
                }
            }
        }
        .searchable(text: $searchText, prompt: "Search disk images...")
        .task {
            // Scan library once when view first appears (not on every re-appearance)
            guard !hasScanned else { return }
            hasScanned = true
            if !appState.configStore.diskImageDirectories.isEmpty {
                await appState.libraryService.scanAll()
            }
        }
    }

}

// MARK: - Empty State

struct EmptyLibraryView: View {
    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "externaldrive")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text("No Disk Images Found")
                .font(.title3)
                .fontWeight(.medium)
            Text("Add a folder containing Apple II disk images\nin Settings to get started.")
                .font(.body)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// MARK: - List View (uses List instead of Table for reliable click handling)

struct LibraryListView: View {
    let items: [LibraryItem]
    @Binding var selection: LibraryItem.ID?
    var onLaunch: ((LibraryItem) -> Void)?
    var onLaunchWithOptions: ((LibraryItem) -> Void)?
    var onFavoriteToggle: ((LibraryItem) -> Void)?

    var body: some View {
        List(items, selection: $selection) { item in
            LibraryListRow(item: item, onLaunch: onLaunch, onLaunchWithOptions: onLaunchWithOptions, onFavoriteToggle: onFavoriteToggle)
                .tag(item.id)
        }
        .listStyle(.inset(alternatesRowBackgrounds: true))
    }
}

struct LibraryListRow: View {
    let item: LibraryItem
    var onLaunch: ((LibraryItem) -> Void)?
    var onLaunchWithOptions: ((LibraryItem) -> Void)?
    var onFavoriteToggle: ((LibraryItem) -> Void)?

    var body: some View {
        HStack(spacing: 12) {
            // Favorite star
            Image(systemName: item.isFavorite ? "star.fill" : "star")
                .font(.caption)
                .foregroundStyle(item.isFavorite ? .yellow : .clear)
                .frame(width: 16)

            // Disk icon
            Image(systemName: diskIcon(for: item.mediaType))
                .font(.callout)
                .foregroundStyle(.secondary)
                .frame(width: 20)

            // Title
            VStack(alignment: .leading, spacing: 2) {
                Text(item.title)
                    .lineLimit(1)
                HStack(spacing: 6) {
                    Text(item.mediaType.displayName)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Text("·")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                    Text(ByteCountFormatter.string(fromByteCount: item.fileSize, countStyle: .file))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Spacer()

            // Play count
            if item.playCount > 0 {
                Text("\(item.playCount) plays")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }

            // Play button — uses .borderless and high-priority gesture
            Button {
                print("[EmuBuddy] List row play: \(item.title)")
                onLaunch?(item)
            } label: {
                Image(systemName: "play.circle.fill")
                    .font(.title3)
                    .foregroundStyle(Color.accentColor)
            }
            .buttonStyle(.borderless)
        }
        .padding(.vertical, 4)
        .contextMenu {
            Button("Play") {
                onLaunch?(item)
            }
            Button("Launch with...") {
                onLaunchWithOptions?(item)
            }
            Divider()
            Button(item.isFavorite ? "Remove from Favorites" : "Add to Favorites") {
                onFavoriteToggle?(item)
            }
            Divider()
            Button("Reveal in Finder") {
                NSWorkspace.shared.activateFileViewerSelecting([item.url])
            }
        }
    }

    private func diskIcon(for mediaType: MediaType) -> String {
        switch mediaType {
        case .hdv: return "externaldrive.fill"
        case .woz: return "opticaldisc.fill"
        default: return "opticaldisc"
        }
    }
}

// MARK: - Grid View

struct LibraryGridView: View {
    let items: [LibraryItem]
    @Binding var selection: LibraryItem.ID?
    var onLaunch: ((LibraryItem) -> Void)?
    var onLaunchWithOptions: ((LibraryItem) -> Void)?
    var onFavoriteToggle: ((LibraryItem) -> Void)?

    let columns = [GridItem(.adaptive(minimum: 150, maximum: 200), spacing: 16)]

    var body: some View {
        ScrollView {
            LazyVGrid(columns: columns, spacing: 16) {
                ForEach(items) { item in
                    LibraryGridItemView(
                        item: item,
                        isSelected: selection == item.id,
                        onSelect: { selection = item.id },
                        onPlay: {
                            print("[EmuBuddy] Grid play: \(item.title)")
                            onLaunch?(item)
                        }
                    )
                    .contextMenu {
                        Button("Play") {
                            onLaunch?(item)
                        }
                        Button("Launch with...") {
                            onLaunchWithOptions?(item)
                        }
                        Divider()
                        Button(item.isFavorite ? "Remove from Favorites" : "Add to Favorites") {
                            onFavoriteToggle?(item)
                        }
                        Divider()
                        Button("Reveal in Finder") {
                            NSWorkspace.shared.activateFileViewerSelecting([item.url])
                        }
                    }
                }
            }
            .padding()
        }
    }
}

struct LibraryGridItemView: View {
    let item: LibraryItem
    let isSelected: Bool
    var onSelect: (() -> Void)?
    var onPlay: (() -> Void)?

    @State private var isHovering = false

    var body: some View {
        VStack(spacing: 8) {
            // Artwork placeholder with play overlay on hover
            ZStack {
                RoundedRectangle(cornerRadius: 8)
                    .fill(.quaternary)
                    .aspectRatio(1, contentMode: .fit)

                Image(systemName: diskIcon(for: item.mediaType))
                    .font(.system(size: 32))
                    .foregroundStyle(.secondary)

                // Favorite badge (top right)
                if item.isFavorite {
                    VStack {
                        HStack {
                            Spacer()
                            Image(systemName: "star.fill")
                                .font(.caption)
                                .foregroundStyle(.yellow)
                                .padding(4)
                        }
                        Spacer()
                    }
                }

                // Play button — always visible, brighter on hover
                VStack {
                    Spacer()
                    Button {
                        onPlay?()
                    } label: {
                        Image(systemName: "play.circle.fill")
                            .font(.title)
                            .foregroundStyle(isHovering ? Color.accentColor : .white.opacity(0.7))
                            .shadow(radius: 2)
                    }
                    .buttonStyle(.plain)
                    .padding(.bottom, 8)
                }
            }
            .onHover { hovering in
                isHovering = hovering
            }
            .contentShape(Rectangle())
            .onTapGesture {
                onSelect?()
            }

            Text(item.title)
                .font(.caption)
                .lineLimit(2)
                .multilineTextAlignment(.center)

            Text(item.mediaType.fileExtension.uppercased())
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
        .padding(8)
        .background(isSelected ? Color.accentColor.opacity(0.15) : Color.clear)
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }

    private func diskIcon(for mediaType: MediaType) -> String {
        switch mediaType {
        case .hdv: return "externaldrive.fill"
        case .woz: return "opticaldisc.fill"
        default: return "opticaldisc"
        }
    }
}
