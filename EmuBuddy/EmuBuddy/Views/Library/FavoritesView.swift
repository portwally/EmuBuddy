import SwiftUI

/// Shows favorited disk images.
struct FavoritesView: View {
    @EnvironmentObject var appState: AppState

    var favoriteItems: [LibraryItem] {
        appState.libraryService.items.filter(\.isFavorite)
    }

    var body: some View {
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
                HStack {
                    Text(item.title)
                        .fontWeight(.medium)
                    Spacer()
                    Text(item.mediaType.displayName)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
        }
    }
}
