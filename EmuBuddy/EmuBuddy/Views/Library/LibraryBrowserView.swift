import SwiftUI

/// Browse disk images in the library with search, filter, and launch.
struct LibraryBrowserView: View {
    @EnvironmentObject var appState: AppState
    @State private var searchText = ""
    @State private var viewMode: ViewMode = .grid
    @State private var selectedItem: LibraryItem?

    /// Callback when user wants to launch a disk image.
    var onLaunch: ((LibraryItem) -> Void)?

    enum ViewMode {
        case list, grid
    }

    var filteredItems: [LibraryItem] {
        appState.libraryService.search(searchText)
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
                        onLaunch?(item)
                    } label: {
                        Label("Play", systemImage: "play.fill")
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.regular)
                }

                Spacer()

                Text("\(filteredItems.count) items")
                    .font(.callout)
                    .foregroundStyle(.secondary)

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
            if filteredItems.isEmpty {
                EmptyLibraryView()
            } else {
                switch viewMode {
                case .list:
                    LibraryListView(items: filteredItems, selection: $selectedItem, onDoubleTap: { item in
                        onLaunch?(item)
                    })
                case .grid:
                    LibraryGridView(items: filteredItems, selection: $selectedItem, onDoubleTap: { item in
                        onLaunch?(item)
                    })
                }
            }
        }
        .searchable(text: $searchText, prompt: "Search disk images...")
        .task {
            await appState.libraryService.scanAll()
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

// MARK: - List View

struct LibraryListView: View {
    let items: [LibraryItem]
    @Binding var selection: LibraryItem?
    var onDoubleTap: ((LibraryItem) -> Void)?

    private var selectionBinding: Binding<LibraryItem.ID?> {
        Binding(
            get: { selection?.id },
            set: { id in selection = items.first { $0.id == id } }
        )
    }

    var body: some View {
        Table(items, selection: selectionBinding) {
            TableColumn("Title", value: \.title)
            TableColumn("Format") { (item: LibraryItem) in
                Text(item.mediaType.displayName)
            }
            .width(min: 80, ideal: 120)
            TableColumn("Size") { (item: LibraryItem) in
                Text(ByteCountFormatter.string(fromByteCount: item.fileSize, countStyle: .file))
            }
            .width(min: 60, ideal: 80)
            TableColumn("") { (item: LibraryItem) in
                Button {
                    onDoubleTap?(item)
                } label: {
                    Image(systemName: "play.circle.fill")
                        .foregroundStyle(Color.accentColor)
                }
                .buttonStyle(.plain)
                .help("Launch with default profile")
            }
            .width(32)
        }
    }
}

// MARK: - Grid View

struct LibraryGridView: View {
    let items: [LibraryItem]
    @Binding var selection: LibraryItem?
    var onDoubleTap: ((LibraryItem) -> Void)?

    let columns = [GridItem(.adaptive(minimum: 150, maximum: 200), spacing: 16)]

    var body: some View {
        ScrollView {
            LazyVGrid(columns: columns, spacing: 16) {
                ForEach(items) { item in
                    LibraryGridItemView(item: item, isSelected: selection?.id == item.id)
                        .onTapGesture {
                            selection = item
                        }
                        .onTapGesture(count: 2) {
                            onDoubleTap?(item)
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

    var body: some View {
        VStack(spacing: 8) {
            // Artwork placeholder
            RoundedRectangle(cornerRadius: 8)
                .fill(.quaternary)
                .aspectRatio(1, contentMode: .fit)
                .overlay {
                    Image(systemName: diskIcon(for: item.mediaType))
                        .font(.system(size: 32))
                        .foregroundStyle(.secondary)
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
