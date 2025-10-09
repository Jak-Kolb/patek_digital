# iOS App (SwiftUI)

This SwiftUI skeleton demonstrates where the future native companion experience will live.

## Requirements
- Xcode 15+
- iOS 16+ simulator or device

## Build
```bash
cd apps/ios
open Package.swift
```
This opens the Swift Package in Xcode. Create a new iOS App scheme pointing to the `App` target.

## TODOs
- Integrate Supabase via a shared Swift client or GraphQL layer.
- Surface BLE pairing status and last transfer timestamps.
- Flesh out navigation, device settings, and offline caching once APIs stabilize.
