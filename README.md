# ESP32 Data Node Workspace

Mono-repo containing the ESP32 firmware, Fastify backend scaffold, Next.js web portal, and SwiftUI iOS companion for the ESP32 Data Node project.

## Repository Layout
- `firmware/` — PlatformIO Arduino project targeting ESP32 DevKit boards.
- `backend/` — Fastify + TypeScript server ready to ingest payloads and push to Supabase.
- `apps/web/` — Next.js placeholder app for visualizing stored telemetry.
- `apps/ios/` — SwiftUI skeleton meant for future native experiences.
- `.vscode/` — Workspace settings and extension recommendations.

## Prerequisites
- Python 3.9+ with PlatformIO Core (`pip install platformio`).
- Node.js 20+ for backend/web work.
- Xcode 15+ for the SwiftUI sample.
- Optional: `arduino-cli` configured via `firmware/arduino-cli.yaml`.

## Common Tasks
### Firmware
```bash
cd firmware
./scripts/erase.sh
./scripts/build_flash.sh
./scripts/monitor.sh
```

### Backend
```bash
cd backend
pnpm install
pnpm dev
```

### Web
```bash
cd apps/web
pnpm install
pnpm dev
```

### iOS
Open `apps/ios/Package.swift` in Xcode and run the `App` target.

## Next Steps
- Implement the real consolidation pipeline in `firmware/lib/compute/consolidate.cpp`.
- Add Supabase persistence in `backend/src/routes/ingest.ts`.
- Hook up Supabase fetching in both the Next.js and SwiftUI clients.
- Expand BLE commands if a richer transport protocol is required.

## License
Released under the MIT License. See `LICENSE` for details.
