// node --test services/link-worker/test   (the daily update-check count: GET /latest.json)
import assert from "node:assert/strict";
import { test } from "node:test";
import worker from "../src/index.js";
import { fakeEnv } from "./fake-env.mjs";

const BASE = "https://link.sculptforge.app";
const FEED = JSON.stringify({ version: "0.12.16", download: "https://sculptforge.app/#download" });

// The website's latest.json; `calls` keeps what the Worker asked it (to check no client header is passed on).
function withFeed(fn, ok = true) {
    const real = globalThis.fetch, calls = [];
    globalThis.fetch = async (url, init) => {
        calls.push({ url: String(url), headers: [...new Headers(init?.headers)] });
        return ok ? new Response(FEED, { headers: { "Content-Type": "application/json" } }) : new Response("no", { status: 503 });
    };
    return fn(calls).finally(() => { globalThis.fetch = real; });
}
const check = (env, query, headers = {}, method = "GET") => worker.fetch(new Request(`${BASE}/latest.json${query}`, { method, headers }), env);

test("latest.json is the website's body, and each check is one data point: version and system", () => withFeed(async () => {
    const env = fakeEnv();
    const r = await check(env, "?v=0.12.16&os=linux");
    assert.equal(r.status, 200);
    assert.equal(await r.text(), FEED);
    await check(env, "?v=0.13.0-beta.1&os=macos");
    assert.deepEqual(env.UPDATE_CHECKS.points, [
        { blobs: ["0.12.16", "linux"], doubles: [1] },
        { blobs: ["0.13.0-beta.1", "macos"], doubles: [1] },
    ]);
    await check(env, "?v=0.12.16&os=linux", {}, "HEAD"); // (not a check)
    assert.equal(env.UPDATE_CHECKS.points.length, 2);
}));

test("odd values are counted as 'other'", () => withFeed(async () => {
    const env = fakeEnv();
    await check(env, "?v=<script>&os=plan9");
    await check(env, "");
    assert.deepEqual(env.UPDATE_CHECKS.points.map((p) => p.blobs), [["other", "other"], ["other", "other"]]);
}));

test("no IP address, user agent, header or id is stored or passed on", () => withFeed(async (calls) => {
    const env = fakeEnv();
    const r = await check(env, "?v=0.12.16&os=linux", {
        "CF-Connecting-IP": "203.0.113.77", "X-Forwarded-For": "203.0.113.78", "User-Agent": "SecretAgent/9.9",
        Cookie: "id=abc123", "X-Request-Id": "req-4242",
    });
    assert.equal(r.headers.get("Set-Cookie"), null);
    assert.deepEqual(env.UPDATE_CHECKS.points, [{ blobs: ["0.12.16", "linux"], doubles: [1] }]); // the whole record
    for (const c of calls) { // the request to the website carries nothing of the app's
        const sent = JSON.stringify(c);
        for (const needle of ["203.0.113", "SecretAgent", "abc123", "req-4242"]) assert.ok(!sent.includes(needle), needle);
    }
}));

test("the website unreachable: 502 and nothing counted (the app falls back to the site)", () => withFeed(async () => {
    const env = fakeEnv();
    assert.equal((await check(env, "?v=0.12.16&os=linux")).status, 502);
    assert.equal(env.UPDATE_CHECKS.points.length, 0);
}, false));
