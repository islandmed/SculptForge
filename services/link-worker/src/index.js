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
export const MAX_BLOB_BYTES = 48 * 1024 * 1024; // an AR model (encrypted GLB), usually a few MB
const BLOB_CHUNK = 1024 * 1024;                     // Durable Object storage values are limited to 2 MB
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

    // The AR model for Android Chrome, which can't reach the computer on the home network: the desktop posts it
    // encrypted (AES-GCM, key only in the AR page's #fragment), the page polls the version and downloads it. A newer
    // version replaces an older one; posting keeps the topic alive for another TTL.
    // `name` "blob": the encrypted model; "share": a readable copy the AR page posts when the person picks Google's
    // AR viewer (Scene Viewer), which can only load a model from a public HTTPS address.
    async putBlob(version, bytes, name = "blob") {
        const meta = await this.ctx.storage.get(name);
        if (meta && meta.version >= version) return { version: meta.version, stale: true };
        const chunks = Math.ceil(bytes.byteLength / BLOB_CHUNK);
        for (let i = 0; i < chunks; ++i) await this.ctx.storage.put(`${name}:${i}`, bytes.slice(i * BLOB_CHUNK, (i + 1) * BLOB_CHUNK));
        await this.ctx.storage.put(name, { version, bytes: bytes.byteLength, chunks });
        if (name === "blob" || (await this.ctx.storage.getAlarm()) === null) await this.ctx.storage.setAlarm(Date.now() + TTL_MS);
        return { version };
    }

    async getBlob(withData, name = "blob") {
        const meta = await this.ctx.storage.get(name);
        if (!meta) return null;
        if (!withData) return { meta };
        const data = new Uint8Array(meta.bytes);
        for (let i = 0, at = 0; i < meta.chunks; ++i) {
            const chunk = new Uint8Array(await this.ctx.storage.get(`${name}:${i}`));
            data.set(chunk, at);
            at += chunk.byteLength;
        }
        return { meta, data };
    }

    async alarm() {
        await this.ctx.storage.deleteAll();
    }

    // The Worker reaches a topic through fetch, not RPC: RPC needs `extends DurableObject` from
    // "cloudflare:workers", which plain Node can't import, and the tests run this file under plain Node.
    async fetch(request) {
        const url = new URL(request.url);
        if (url.pathname === "/blob" || url.pathname === "/share") {
            const name = url.pathname.slice(1);
            if (request.method === "POST")
                return Response.json(await this.putBlob(Number(url.searchParams.get("v")), await request.arrayBuffer(), name));
            const blob = await this.getBlob(url.searchParams.has("data"), name);
            if (!blob) return Response.json({ error: "no model yet" }, { status: 404 });
            if (!blob.data) return Response.json(blob.meta);
            return new Response(blob.data, { headers: { "X-Version": String(blob.meta.version) } });
        }
        if (request.method === "POST") return Response.json(await this.post(url.searchParams.get("topic"), await request.text()));
        return Response.json(await this.poll(Number(url.searchParams.get("since"))));
    }
}

// What a person sees who opens link.sculptforge.app in a browser (a phone may save plain text as a file instead).
const HOME_PAGE = `<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>SculptForge link relay</title>
<style>body{font:17px/1.5 system-ui,sans-serif;max-width:34rem;margin:15vh auto;padding:0 16px;color:#222;background:#fafafa}
@media (prefers-color-scheme:dark){body{color:#eee;background:#161616}a{color:#f90}}</style></head><body>
<h1>SculptForge link relay</h1>
<p>This address passes one small encrypted message between your phone and SculptForge on your computer when you
connect them (File &gt; Capture from phone). There is nothing to do here.</p>
<p><a href="https://sculptforge.app/">sculptforge.app</a> &middot;
<a href="https://github.com/islandmed/SculptForge/blob/main/docs/phone-link.md">how it works</a></p>
</body></html>
`;

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
        headers["Access-Control-Allow-Methods"] = "GET, POST, HEAD";
        headers["Access-Control-Allow-Headers"] = "Content-Type"; // the AR page posts model/gltf-binary
        headers["Access-Control-Expose-Headers"] = "X-Version";
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
            return respond(HOME_PAGE, 200, origin, "text/html; charset=utf-8");

        const blobMatch = url.pathname.match(/^\/blob\/([^/]+)$/);
        if (blobMatch) {
            const topic = blobMatch[1];
            if (!TOPIC_PATTERN.test(topic)) return json({ error: "bad topic" }, 400, origin);
            const stub = env.TOPICS.get(env.TOPICS.idFromName(topic));
            if (request.method === "POST") {
                if (origin) return json({ error: "only SculptForge posts models" }, 403, origin); // not from a page
                const version = Number(url.searchParams.get("v"));
                if (!Number.isSafeInteger(version) || version < 1) return json({ error: "bad version" }, 400, origin);
                if (Number(request.headers.get("Content-Length") ?? "0") > MAX_BLOB_BYTES) return json({ error: "too large" }, 413, origin);
                const body = await request.arrayBuffer();
                if (body.byteLength > MAX_BLOB_BYTES) return json({ error: "too large" }, 413, origin);
                if (body.byteLength < 12 + 16) return json({ error: "not an encrypted model" }, 400, origin);
                const r = await stub.fetch(new Request(`https://topic/blob?v=${version}`, { method: "POST", body }));
                return json(await r.json(), 200, origin);
            }
            if (request.method !== "GET") return json({ error: "method not allowed" }, 405, origin);
            const data = url.searchParams.has("data");
            const r = await stub.fetch(new Request(`https://topic/blob${data ? "?data=1" : ""}`));
            if (r.status === 404) return json({ error: "no model yet" }, 404, origin);
            if (!data) return respond(await r.text(), 200, origin);
            const out = respond(await r.arrayBuffer(), 200, origin, "application/octet-stream");
            out.headers.set("X-Version", r.headers.get("X-Version"));
            return out;
        }

        // A readable copy for Scene Viewer: posted by the AR page (after the person taps Google's AR viewer), read by
        // Google's app, which fetches it itself. Deleted with the topic, 10 minutes after the first model.
        const shareMatch = url.pathname.match(/^\/share\/([^/]+)\/model\.glb$/);
        if (shareMatch) {
            const topic = shareMatch[1];
            if (!TOPIC_PATTERN.test(topic)) return json({ error: "bad topic" }, 400, origin);
            const stub = env.TOPICS.get(env.TOPICS.idFromName(topic));
            if (request.method === "POST") {
                if (!origin) return json({ error: "posted by the AR page only" }, 403, origin);
                const version = Number(url.searchParams.get("v"));
                if (!Number.isSafeInteger(version) || version < 1) return json({ error: "bad version" }, 400, origin);
                const body = await request.arrayBuffer();
                if (body.byteLength > MAX_BLOB_BYTES) return json({ error: "too large" }, 413, origin);
                if (new TextDecoder().decode(new Uint8Array(body, 0, Math.min(4, body.byteLength))) !== "glTF")
                    return json({ error: "not a GLB model" }, 400, origin);
                if (!(await stub.fetch(new Request("https://topic/blob"))).ok) return json({ error: "no model for this link" }, 404, origin);
                const r = await stub.fetch(new Request(`https://topic/share?v=${version}`, { method: "POST", body }));
                return json(await r.json(), 200, origin);
            }
            if (request.method !== "GET" && request.method !== "HEAD") return json({ error: "method not allowed" }, 405, origin);
            const r = await stub.fetch(new Request("https://topic/share?data=1"));
            if (r.status === 404) return json({ error: "no model" }, 404, origin);
            return new Response(request.method === "HEAD" ? null : await r.arrayBuffer(), {
                headers: { "Content-Type": "model/gltf-binary", "Cache-Control": "no-store", "Access-Control-Allow-Origin": "*",
                           "X-Content-Type-Options": "nosniff" },
            });
        }

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
