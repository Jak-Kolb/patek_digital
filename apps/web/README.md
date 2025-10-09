# Web App (Next.js)

A minimal Next.js application that will eventually surface device payloads stored in Supabase.

## Setup
```bash
cd apps/web
pnpm install            # or npm install
```

Create a `.env.local` with:
```bash
NEXT_PUBLIC_SUPABASE_URL=https://your-project.supabase.co
NEXT_PUBLIC_SUPABASE_ANON_KEY=your-anon-key
```

## Development
```bash
pnpm dev
```

## TODOs
- Wire up `lib/supabase.ts` to fetch consolidated payload rows.
- Add authentication/authorization once requirements are clear.
- Design UI components for charts, device management, and BLE transfer status.
