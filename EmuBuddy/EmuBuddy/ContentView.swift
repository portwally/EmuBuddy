import SwiftUI

/// Main window: sidebar navigation + detail content.
struct ContentView: View {
    @EnvironmentObject var appState: AppState
    @State private var showSetupWizard = false
    @State private var showLaunchSheet = false
    @State private var launchItem: LibraryItem?

    var body: some View {
        Group {
            if let session = appState.activeSession {
                EmulationSessionView(session: session)
            } else {
                NavigationSplitView {
                    SidebarView(selection: $appState.selectedSidebarItem)
                } detail: {
                    switch appState.selectedSidebarItem {
                    case .library:
                        LibraryBrowserView(onLaunch: { item in
                            launchItem = item
                            showLaunchSheet = true
                        })
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
            }
        }
        .navigationTitle("EmuBuddy")
        .frame(minWidth: 900, minHeight: 600)
        .sheet(isPresented: $showSetupWizard) {
            SetupWizardView()
                .environmentObject(appState)
        }
        .sheet(isPresented: $showLaunchSheet) {
            if let item = launchItem {
                QuickLaunchView(item: item) { profile, media in
                    showLaunchSheet = false
                    Task {
                        await appState.launchSession(profile: profile, media: media)
                    }
                }
                .environmentObject(appState)
            }
        }
        .onAppear {
            if !appState.isMAMEConfigured {
                showSetupWizard = true
            }
        }
        .onChange(of: appState.isMAMEConfigured) { _, newValue in
            if newValue {
                showSetupWizard = false
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
