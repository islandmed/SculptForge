// SculptForge link relay: a Cloudflare Worker that carries the one message the phone's face page sends to the
// desktop to set up the WebRTC connection (see docs/phone-link.md). It speaks the small part of ntfy.sh's protocol
// the desktop already uses, so either can serve as the relay:
//
//   POST /relay/<topic>                      body: the message (base64url text, at most 8 KB)
//   GET  /relay/<topic>/json?poll=1&since=<unix seconds>
//        -> one JSON object per line: {"id","time","event":"message","topic","message"}
//
// The message is AES-256-GCM encrypted by the phone with a key only the phone and the desktop know (it travels in
// the face link's #fragment), so the relay can neither read nor forge it. Each topic is a Durable Object (strongly
// consistent, unlike KV: the phone and the desktop may reach different Cloudflare locations); its messages are
// deleted 10 minutes after the first one.

export const TOPIC_PATTERN = /^sculptforge-[0-9a-f]{24}$/;
export const MAX_MESSAGE_BYTES = 8192;
export const MAX_MESSAGES = 4;          // per topic: the answer, and a few retries
export const TTL_MS = 10 * 60 * 1000;
const MESSAGE_PATTERN = /^[A-Za-z0-9_-]+$/; // base64url: the relay carries opaque ciphertext only

export class Topic {
    constructor(ctx, env) {
        this.ctx = ctx;
        this.env = env;
    }

    // Returns the stored message's id, or an error string ("full").
    async post(topic, message) {
        const messages = (await this.ctx.storage.get("messages")) ?? [];
        if (messages.length >= MAX_MESSAGES) return { error: "full" };
        const entry = {
            id: crypto.randomUUID().replaceAll("-", "").slice(0, 12),
            time: Math.floor(Date.now() / 1000),
            event: "message",
            topic,
            message,
        };
        messages.push(entry);
        await this.ctx.storage.put("messages", messages);
        if ((await this.ctx.storage.getAlarm()) === null) await this.ctx.storage.setAlarm(Date.now() + TTL_MS);
        return { id: entry.id, time: entry.time };
    }

    async poll(since) {
        const messages = (await this.ctx.storage.get("messages")) ?? [];
        const fresh = Math.floor((Date.now() - TTL_MS) / 1000); // in case the alarm runs late
        return messages.filter((m) => m.time >= since && m.time >= fresh);
    }

    async alarm() {
        await this.ctx.storage.deleteAll();
    }

    // The Worker reaches a topic through fetch, not RPC: RPC needs `extends DurableObject` from
    // "cloudflare:workers", which plain Node can't import, and the tests run this file under plain Node.
    async fetch(request) {
        const url = new URL(request.url);
        if (request.method === "POST") return Response.json(await this.post(url.searchParams.get("topic"), await request.text()));
        return Response.json(await this.poll(Number(url.searchParams.get("since"))));
    }
}

function allowedOrigin(request, env) {
    const origin = request.headers.get("Origin");
    if (!origin) return null;
    const allowed = (env.ALLOWED_ORIGINS ?? "").split(",").map((s) => s.trim()).filter(Boolean);
    return allowed.includes(origin) ? origin : undefined; // undefined: a browser page we don't serve
}

function respond(body, status, origin, type = "application/json") {
    const headers = { "Content-Type": type, "Cache-Control": "no-store", "X-Content-Type-Options": "nosniff" };
    if (origin) {
        headers["Access-Control-Allow-Origin"] = origin;
        headers["Access-Control-Allow-Methods"] = "GET, POST";
        headers["Access-Control-Max-Age"] = "86400";
        headers["Vary"] = "Origin";
    }
    return new Response(body, { status, headers });
}

const json = (value, status, origin) => respond(JSON.stringify(value), status, origin);

export default {
    async fetch(request, env) {
        const url = new URL(request.url);
        const origin = allowedOrigin(request, env);
        if (origin === undefined) return json({ error: "origin not allowed" }, 403, null);
        if (request.method === "OPTIONS") return respond(null, 204, origin);
        if (url.pathname === "/" || url.pathname === "/relay" || url.pathname === "/relay/")
            return respond("SculptForge link relay. See https://github.com/islandmed/SculptForge/blob/main/docs/phone-link.md\n",
                           200, origin, "text/plain; charset=utf-8");

        const match = url.pathname.match(/^\/relay\/([^/]+)(\/json)?$/);
        if (!match) return json({ error: "not found" }, 404, origin);
        const [, topic, poll] = match;
        if (!TOPIC_PATTERN.test(topic)) return json({ error: "bad topic" }, 400, origin);
        const stub = env.TOPICS.get(env.TOPICS.idFromName(topic));
        const call = async (query, init) => (await stub.fetch(new Request(`https://topic/?${query}`, init))).json();

        if (poll) {
            if (request.method !== "GET") return json({ error: "method not allowed" }, 405, origin);
            const sinceText = url.searchParams.get("since") ?? "0";
            const since = sinceText === "all" ? 0 : Number(sinceText);
            if (!Number.isFinite(since)) return json({ error: "bad since" }, 400, origin);
            const messages = await call(`since=${since}`);
            return respond(messages.map((m) => JSON.stringify(m) + "\n").join(""), 200, origin, "application/x-ndjson");
        }

        if (request.method !== "POST" && request.method !== "PUT") return json({ error: "method not allowed" }, 405, origin);
        const declared = Number(request.headers.get("Content-Length") ?? "0");
        if (declared > MAX_MESSAGE_BYTES) return json({ error: "too large" }, 413, origin);
        const message = (await request.text()).trim();
        if (message.length > MAX_MESSAGE_BYTES) return json({ error: "too large" }, 413, origin);
        if (!MESSAGE_PATTERN.test(message)) return json({ error: "the message must be base64url" }, 400, origin);
        const result = await call(`topic=${topic}`, { method: "POST", body: message });
        if (result.error) return json({ error: "too many messages for this topic" }, 429, origin);
        return json({ id: result.id, time: result.time, event: "message", topic }, 200, origin);
    },
};
