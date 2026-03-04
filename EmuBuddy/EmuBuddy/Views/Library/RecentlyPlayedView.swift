import SwiftUI

/// Shows recently played disk images, sorted by last played date.
struct RecentlyPlayedView: View {
    @EnvironmentObject var appState: AppState

    var recentItems: [LibraryItem] {
        appState.libraryService.items
            .filter { $0.lastPlayed != nil }
            .sorted { ($0.lastPlayed ?? .distantPast) > ($1.lastPlayed ?? .distantPast) }
    }

    var body: some View {
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
                HStack {
                    VStack(alignment: .leading) {
                        Text(item.title)
                            .fontWeight(.medium)
                        if let date = item.lastPlayed {
                            Text(date, style: .relative)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                    Spacer()
                    Text(item.mediaType.displayName)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
    }
}
