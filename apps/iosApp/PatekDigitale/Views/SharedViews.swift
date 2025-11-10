//
//  SharedViews.swift
//  PatekDigitale
//
//  Shared UI components used across multiple views
//

import SwiftUI

// MARK: - Signal Strength View

/// Visual signal strength indicator with colored bars
struct SignalStrengthView: View {
    let rssi: Int
    
    /// Convert RSSI to 0-4 bar scale
    private var barCount: Int {
        switch rssi {
        case -50...0: return 4 // Excellent
        case -60..<(-50): return 3 // Good
        case -70..<(-60): return 2 // Fair
        case -80..<(-70): return 1 // Poor
        default: return 0 // Very poor
        }
    }
    
    var body: some View {
        HStack(spacing: 2) {
            ForEach(0..<4) { index in
                RoundedRectangle(cornerRadius: 2)
                    .fill(index < barCount ? Color.green : Color.gray.opacity(0.3))
                    .frame(width: 4, height: CGFloat(4 + index * 3))
            }
        }
    }
}

// MARK: - Quality Indicator

/// Quality indicator with color-coded icon and text
struct QualityIndicator: View {
    let flags: QualityFlags
    
    var body: some View {
        HStack {
            Image(systemName: flags.isReliable ? "checkmark.circle.fill" : "exclamationmark.triangle.fill")
                .foregroundColor(flags.isReliable ? .green : .orange)
            Text(flags.isReliable ? "Good" : "Issues")
                .foregroundColor(.secondary)
                .font(.caption)
        }
    }
}
