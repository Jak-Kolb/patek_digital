import SwiftUI

@main
struct ESP32DataIOSApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}

struct ContentView: View {
    var body: some View {
        NavigationStack {
            VStack(alignment: .leading, spacing: 16) {
                Text("ESP32 Data Node")
                    .font(.largeTitle)
                    .bold()
                Text("This SwiftUI placeholder will later display consolidated payloads fetched from Supabase.")
                    .font(.body)
                VStack(alignment: .leading, spacing: 8) {
                    Text("TODOs")
                        .font(.headline)
                    Text("• Configure Supabase client with shared credentials.")
                    Text("• Implement BLE pairing instructions or status hand-offs.")
                    Text("• Render payload history once API contracts are finalized.")
                }
                Spacer()
            }
            .padding()
            .navigationTitle("Dashboard")
        }
    }
}
