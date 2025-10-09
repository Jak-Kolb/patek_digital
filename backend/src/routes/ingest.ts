import { FastifyInstance } from 'fastify';
import { z } from 'zod';
import { getSupabaseClient } from '../supabase.js';

const ingestSchema = z.object({
  deviceId: z.string().min(1, 'deviceId required'),
  payloadBase64: z.string().min(1, 'payloadBase64 required'),
  ts: z.string().datetime().or(z.number()),
});

export async function ingestRoute(app: FastifyInstance) {
  const supabase = getSupabaseClient();

  app.post('/ingest', async (request, reply) => {
    const parseResult = ingestSchema.safeParse(request.body);
    if (!parseResult.success) {
      return reply.code(400).send({ errors: parseResult.error.flatten() });
    }

    const { deviceId, payloadBase64, ts } = parseResult.data;

    app.log.info({ deviceId, ts }, 'Received ingest request');

    // TODO: Decode payload and store in Supabase "measurements" table.
    void payloadBase64; // placeholder until implementation

    if (!supabase) {
      return reply.code(202).send({ status: 'accepted', hint: 'Supabase client unavailable; check env' });
    }

    // TODO: supabase.from('measurements').insert([...])

    return reply.code(202).send({ status: 'accepted', deviceId });
  });
}
