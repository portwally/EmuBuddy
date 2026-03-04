import SwiftUI

/// Browse disk images in the library with search, filter, and launch.
struct LibraryBrowserView: View {
    @EnvironmentObject var appState: AppState
    @State private var searchText = ""
    @State private var viewMode: ViewMode = .grid
    @State private var selectedItem: LibraryItem?

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

                Spacer()

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
                    LibraryListView(items: filteredItems, selection: $selectedItem)
                case .grid:
                    LibraryGridView(items: filteredItems, selection: $selectedItem)
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

    var body: some View {
        Table(items, selection: Binding(
            get: { selection?.id },
            set: { id in selection = items.first { $0.id == id } }
        )) {
            TableColumn("Title") { item in
                Text(item.title)
            }
            TableColumn("Format") { item in
                Text(item.mediaType.displayName)
            }
            .width(min: 80, ideal: 120)
            TableColumn("Size") { item in
                Text(ByteCountFormatter.string(fromByteCount: item.fileSize, countStyle: .file))
            }
            .width(min: 60, ideal: 80)
        }
    }
}

// MARK: - Grid View (Placeholder)

struct LibraryGridView: View {
    let items: [LibraryItem]
    @Binding var selection: LibraryItem?

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
                            // TODO: Launch emulation session
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
                    Image(systemName: "opticaldisc")
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
}
