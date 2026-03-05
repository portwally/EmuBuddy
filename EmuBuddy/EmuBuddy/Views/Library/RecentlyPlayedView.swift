import SwiftUI

/// Shows recently played disk images, sorted by last played date.
struct RecentlyPlayedView: View {
    @EnvironmentObject var appState: AppState

    var onLaunch: ((LibraryItem) -> Void)?

    var recentItems: [LibraryItem] {
        appState.libraryService.items
            .filter { $0.lastPlayed != nil }
            .sorted { ($0.lastPlayed ?? .distantPast) > ($1.lastPlayed ?? .distantPast) }
    }

    var body: some View {
        Group {
            if recentItems.isEmpty {
                VStack(spacing: 16) {
                    Image(systemName: "clock")
                        .font(.system(size: 48))
                        .foregroundStyle(.secondary)
                    Text("No Recently Played Items")
                        .font(.title3)
                    Text("Disk images you play will appear here.")
                        .foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List(recentItems) { item in
                    RecentItemRow(item: item) {
                        onLaunch?(item)
                    }
                    .contextMenu {
                        Button("Play") { onLaunch?(item) }
                        Divider()
                        Button(item.isFavorite ? "Remove from Favorites" : "Add to Favorites") {
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
        .navigationTitle("Recently Played")
    }
}

struct RecentItemRow: View {
    let item: LibraryItem
    let onPlay: () -> Void

    var body: some View {
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
                    if let date = item.lastPlayed {
                        Text(date, style: .relative)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    if item.playCount > 1 {
                        Text("Played \(item.playCount) times")
                            .font(.caption)
                            .foregroundStyle(.tertiary)
                    }
                }
            }

            Spacer()

            Text(item.mediaType.displayName)
                .font(.caption)
                .foregroundStyle(.secondary)
                .padding(.horizontal, 8)
                .padding(.vertical, 2)
                .background(.quaternary)
                .clipShape(Capsule())

            if item.isFavorite {
                Image(systemName: "star.fill")
                    .font(.caption)
                    .foregroundStyle(.yellow)
            }

            Button(action: onPlay) {
                Image(systemName: "play.fill")
                    .font(.caption)
            }
            .buttonStyle(.bordered)
            .controlSize(.small)
        }
        .padding(.vertical, 4)
    }

    func iconName(for mediaType: MediaType) -> String {
        switch mediaType {
        case .hdv: return "externaldrive.fill"
        case .woz: return "opticaldisc.fill"
        default:   return "opticaldisc"
        }
    }
}
