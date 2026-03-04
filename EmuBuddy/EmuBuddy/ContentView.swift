import SwiftUI

/// Main window: sidebar navigation + detail content.
struct ContentView: View {
    @EnvironmentObject var appState: AppState

    var body: some View {
        NavigationSplitView {
            SidebarView(selection: $appState.selectedSidebarItem)
        } detail: {
            switch appState.selectedSidebarItem {
            case .library:
                LibraryBrowserView()
            case .machines:
                MachineListView()
            case .recentlyPlayed:
                RecentlyPlayedView()
            case .favorites:
                FavoritesView()
            case .settings:
                SettingsView()
            }
        }
        .navigationTitle("EmuBuddy")
        .frame(minWidth: 900, minHeight: 600)
        .onAppear {
            if !appState.isMAMEConfigured {
                // TODO: Show first-run setup wizard
            }
        }
    }
}

// MARK: - Sidebar

struct SidebarView: View {
    @Binding var selection: SidebarItem

    var body: some View {
        List(SidebarItem.allCases, selection: $selection) { item in
            Label(item.rawValue, systemImage: item.systemImage)
                .tag(item)
        }
        .listStyle(.sidebar)
        .frame(minWidth: 180)
    }
}
