import SwiftUI

/// Shows favorited disk images.
struct FavoritesView: View {
    @EnvironmentObject var appState: AppState

    var onLaunch: ((LibraryItem) -> Void)?

    var favoriteItems: [LibraryItem] {
        appState.libraryService.items
            .filter(\.isFavorite)
            .sorted { $0.title.localizedStandardCompare($1.title) == .orderedAscending }
    }

    var body: some View {
        Group {
            if favoriteItems.isEmpty {
                VStack(spacing: 16) {
                    Image(systemName: "star")
                        .font(.system(size: 48))
                        .foregroundStyle(.secondary)
                    Text("No Favorites Yet")
                        .font(.title3)
                    Text("Star disk images in your library to find them here.")
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(favoriteItems) { item in
                    HStack(spacing: 12) {
                        Image(systemName: iconName(for: item.mediaType))
                            .font(.title2)
                            .foregroundStyle(.tint)
                            .frame(width: 32)

                        VStack(alignment: .leading, spacing: 2) {
                            Text(item.title)
                                .fontWeight(.medium)
                                .lineLimit(1)
                            HStack(spacing: 8) {
                                Text(item.mediaType.displayName)
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                                if item.playCount > 0 {
                                    Text("Played \(item.playCount) time\(item.playCount == 1 ? "" : "s")")
                                        .font(.caption)
                                        .foregroundStyle(.tertiary)
                                }
                            }
                        }

                        Spacer()

                        Button(action: { onLaunch?(item) }) {
                            Image(systemName: "play.fill")
                                .font(.caption)
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)

                        Button(action: {
                            appState.libraryService.toggleFavorite(for: item)
                        }) {
                            Image(systemName: "star.slash")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        .buttonStyle(.plain)
                        .help("Remove from Favorites")
                    }
                    .padding(.vertical, 4)
                    .contextMenu {
                        Button("Play") { onLaunch?(item) }
                        Divider()
                        Button("Remove from Favorites") {
                            appState.libraryService.toggleFavorite(for: item)
                        }
                        Divider()
                        Button("Reveal in Finder") {
                            NSWorkspace.shared.activateFileViewerSelecting([item.url])
                        }
                    }
                }
                .listStyle(.inset(alternatesRowBackgrounds: true))
            }
        }
        .navigationTitle("Favorites")
    }

    func iconName(for mediaType: MediaType) -> String {
        switch mediaType {
        case .hdv: return "externaldrive.fill"
        case .woz: return "opticaldisc.fill"
        default:   return "opticaldisc"
        }
    }
}
