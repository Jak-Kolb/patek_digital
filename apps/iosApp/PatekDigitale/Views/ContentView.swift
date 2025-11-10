//
//  ContentView.swift
//  PatekDigitale
//
//  Main app view with tab-based navigation
//

import SwiftUI
import CoreData

struct ContentView: View {
    // BLE manager instance shared across all tabs
    @StateObject var bleManager = BLEManager()
    
    // Selected tab index
    @State private var selectedTab = 0
    
    var body: some View {
        TabView(selection: $selectedTab) {
            // MARK: - Dashboard Tab
            DashboardView(bleManager: bleManager)
                .tabItem {
                    Label("Dashboard", systemImage: "heart.text.square.fill")
                }
                .tag(0)
            
            // MARK: - History/Charts Tab
            ChartsView()
                .tabItem {
                    Label("History", systemImage: "chart.xyaxis.line")
                }
                .tag(1)
            
            // MARK: - Settings Tab
            NavigationView {
                SettingsTabWrapper(bleManager: bleManager)
            }
            .tabItem {
                Label("Settings", systemImage: "gear")
            }
            .tag(2)
            
            // MARK: - Export Tab
            ExportView()
                .tabItem {
                    Label("Export", systemImage: "square.and.arrow.up")
                }
                .tag(3)
        }
        .accentColor(.blue)
    }
}

// MARK: - Settings Tab Wrapper

/// Wrapper view for settings tab to avoid navigation conflicts
struct SettingsTabWrapper: View {
    @ObservedObject var bleManager: BLEManager
    @Environment(\.managedObjectContext) private var context
    
    var body: some View {
        EnhancedSettingsView(bleManager: bleManager)
            .onAppear {
                bleManager.setContext(context)
            }
    }
}

// MARK: - Preview

struct ContentView_Previews: PreviewProvider {
    static var previews: some View {
        ContentView()
            .environment(\.managedObjectContext, DataController.shared.container.viewContext)
    }
}
