# Backend (Fastify + Supabase)

This directory contains a minimal Fastify server with scaffolding for ingesting ESP32 payloads and forwarding them to Supabase.

## Prerequisites
- Node.js 20+
- pnpm, npm, or yarn

## Setup
```bash
cd backend
pnpm install            # or npm install
cp .env.example .env
```
Populate `.env` with your Supabase URL and anon key.

## Development
```bash
pnpm dev
```
This starts Fastify with hot reload on `http://localhost:3333`.

### Routes
- `GET /healthz` — simple health probe.
- `POST /ingest` — validates `{ deviceId, payloadBase64, ts }` and returns `202 Accepted`.
  - TODO: decode the payload and insert into the `measurements` table via Supabase.

## Production Build
```bash
pnpm build
pnpm start
```

## Notes
- The Supabase client is created lazily; if env vars are missing, ingest requests are accepted but not persisted.
- Extend `src/routes/ingest.ts` once the payload schema and Supabase table are finalized.
