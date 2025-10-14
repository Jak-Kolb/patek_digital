export default function HomePage() {
  return (
    <main className="mx-auto flex min-h-screen max-w-2xl flex-col gap-6 p-8">
      <h1 className="text-3xl font-semibold">ESP32 Data Portal</h1>
      <p>
        This placeholder UI will eventually visualize consolidated payloads
        pulled from Supabase.
      </p>
      <section className="rounded border border-dashed border-gray-400 p-4">
        <h2 className="text-xl font-medium">Next Steps</h2>
        <ul className="list-disc pl-6">
          <li>
            TODO: Instantiate the Supabase client from{" "}
            <code>lib/supabase.ts</code>.
          </li>
          <li>
            TODO: Fetch the latest measurements for a selected{" "}
            <code>deviceId</code>.
          </li>
          <li>TODO: Render charts/tables once payload schema is defined.</li>
        </ul>
      </section>
    </main>
  );
}
