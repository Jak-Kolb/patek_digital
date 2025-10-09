import Fastify from 'fastify';
import cors from '@fastify/cors';
import { ingestRoute } from './routes/ingest.js';

const server = Fastify({ logger: true });

await server.register(cors, { origin: true });

server.get('/healthz', async () => ({ status: 'ok' }));

await ingestRoute(server);

const port = Number(process.env.PORT ?? 3333);
const host = process.env.HOST ?? '0.0.0.0';

try {
  await server.listen({ port, host });
  server.log.info(`API listening on http://${host}:${port}`);
} catch (err) {
  server.log.error(err);
  process.exit(1);
}
