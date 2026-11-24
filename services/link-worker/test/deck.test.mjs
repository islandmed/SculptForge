// node --test services/link-worker/test   (no packages needed)
// The Forge Deck's relay fallback: commands up (page -> computer), state down (computer -> page). Opaque ciphertext.
import assert from "node:assert/strict";
import { test } from "node:test";
import worker, { DECK_IDLE_MS, DECK_UP_PER_SECOND, DECK_UP_TTL_MS, MAX_DECK_DOWN_BYTES, MAX_DECK_QUEUE, MAX_DECK_UP_BYTES } from "../src/index.js";
import { fakeEnv } from "./fake-env.mjs";

const BASE = "https://link.sculptforge.app";
const TOPIC = "sculptforge-0123456789abcdef01234567";
const PAGE = "https://sculptforge.app";

const call = (env, way, { method = "GET", body, origin, query = "" } = {}) =>
    worker.fetch(new Request(`${BASE}/deck/${TOPIC}/${way}${query}`, { method, body, headers: origin ? { Origin: origin } : {} }), env);
const page = (env, way, init = {}) => call(env, way, { origin: PAGE, ...init });
const computer = (env, way, init = {}) => call(env, way, init);

test("commands go up from the page and the computer collects them in order, once", async () => {
    const env = fakeEnv();
    for (const m of ["Zmlyc3Q", "c2Vjb25k", "dGhpcmQ"]) assert.equal((await page(env, "up", { method: "POST", body: m })).status, 200);
    const got = await (await computer(env, "up")).json();
    assert.deepEqual(got.map((m) => m.message), ["Zmlyc3Q", "c2Vjb25k", "dGhpcmQ"]);
    assert.deepEqual(got.map((m) => m.n), [1, 2, 3]);
    // The computer says how far it has read: those are deleted, only newer ones come back.
    await page(env, "up", { method: "POST", body: "Zm91cnRo" });
    const next = await (await computer(env, "up", { query: "?after=3" })).json();
    assert.deepEqual(next.map((m) => m.n), [4]);
    assert.deepEqual(await (await computer(env, "up", { query: "?after=4" })).json(), []);
});

test("only the right side posts and reads each direction; sizes, shapes and topics are checked", async () => {
    const env = fakeEnv();
    assert.equal((await computer(env, "up", { method: "POST", body: "abc" })).status, 403);   // commands come from the page
    assert.equal((await page(env, "down", { method: "POST", body: "abc" })).status, 403);     // state comes from SculptForge
    assert.equal((await page(env, "up")).status, 403);                                        // the page doesn't collect commands
    assert.equal((await page(env, "up", { method: "POST", body: "not base64url!" })).status, 400);
    assert.equal((await page(env, "up", { method: "POST", body: "" })).status, 400);
    assert.equal((await page(env, "up", { method: "POST", body: "a".repeat(MAX_DECK_UP_BYTES + 1) })).status, 413);
    assert.equal((await page(env, "up", { method: "POST", body: "a".repeat(MAX_DECK_UP_BYTES) })).status, 200);
    assert.equal((await computer(env, "down", { method: "POST", body: "a".repeat(MAX_DECK_DOWN_BYTES + 1) })).status, 413);
    assert.equal((await call(env, "up", { method: "DELETE", origin: PAGE })).status, 405);
    assert.equal((await page(env, "up", { method: "POST", body: "abc", origin: "https://evil.example" })).status, 403);
    assert.equal((await computer(env, "up", { query: "?after=-1" })).status, 400);
    assert.equal((await worker.fetch(new Request(`${BASE}/deck/not-a-topic/up`), env)).status, 400);
    assert.equal((await worker.fetch(new Request(`${BASE}/deck/${TOPIC}/sideways`), env)).status, 404);
});

test("the latest state comes down to the page; the page asks only for newer ones", async () => {
    const env = fakeEnv();
    assert.deepEqual(await (await page(env, "down")).json(), { n: 0 });
    await computer(env, "down", { method: "POST", body: "c3RhdGUx" });
    await computer(env, "down", { method: "POST", body: "c3RhdGUy" });
    assert.deepEqual(await (await page(env, "down")).json(), { n: 2, message: "c3RhdGUy" }); // the latest only
    assert.deepEqual(await (await page(env, "down", { query: "?after=2" })).json(), { n: 2 });  // nothing new
    const r = await page(env, "down");
    assert.equal(r.headers.get("Access-Control-Allow-Origin"), PAGE);
    assert.equal(r.headers.get("Cache-Control"), "no-store");
});

test("a command nobody collects is dropped after 20 seconds, and a full queue refuses", async () => {
    const env = fakeEnv();
    const realNow = Date.now;
    let t = realNow();
    Date.now = () => t;
    try {
        await page(env, "up", { method: "POST", body: "b2xk" });
        t += DECK_UP_TTL_MS + 1;
        assert.deepEqual(await (await computer(env, "up")).json(), []);
        for (let i = 0; i < MAX_DECK_QUEUE; ++i) {
            if (i % DECK_UP_PER_SECOND === DECK_UP_PER_SECOND - 1) t += 1100; // stay under the per-second limit
            assert.equal((await page(env, "up", { method: "POST", body: `bQ${i}` })).status, 200);
        }
        t += 1100;
        assert.equal((await page(env, "up", { method: "POST", body: "bW9yZQ" })).status, 429);
        await computer(env, "up", { query: "?after=0" });
        assert.equal((await page(env, "up", { method: "POST", body: "bW9yZQ" })).status, 429); // not collected yet: still full
        const all = await (await computer(env, "up", { query: "?after=0" })).json();
        await computer(env, "up", { query: `?after=${all.at(-1).n}` });
        assert.equal((await page(env, "up", { method: "POST", body: "bW9yZQ" })).status, 200);
    } finally {
        Date.now = realNow;
    }
});

test("a flood of commands a second is refused", async () => {
    const env = fakeEnv();
    let refused = 0;
    for (let i = 0; i < DECK_UP_PER_SECOND + 10; ++i) if ((await page(env, "up", { method: "POST", body: "YQ" })).status === 429) ++refused;
    assert.equal(refused, 10);
});

test("the Deck's data is deleted five minutes after the last message", async () => {
    const env = fakeEnv();
    await page(env, "up", { method: "POST", body: "YQ" });
    await computer(env, "down", { method: "POST", body: "YQ" });
    await env.fireAlarms(Date.now() + DECK_IDLE_MS - 5000);
    assert.equal((await (await computer(env, "up", { query: "?after=0" })).json()).length, 1);
    await env.fireAlarms(Date.now() + DECK_IDLE_MS + 5000);
    assert.deepEqual(await (await computer(env, "up", { query: "?after=0" })).json(), []);
    assert.deepEqual(await (await page(env, "down")).json(), { n: 0 });
});

// The page is static and ours; these are the properties the security review leans on.
import { readFileSync } from "node:fs";
const PAGE_HTML = readFileSync(new URL("../../../website/link/deck.html", import.meta.url), "utf8");

test("the Deck page never builds markup from what the computer sends, and says what it may load", () => {
    const script = PAGE_HTML.slice(PAGE_HTML.indexOf("<script>"));
    assert.ok(!/innerHTML\s*=\s*[^'"\s]/.test(script.replace(/tick\.innerHTML = '<input type="checkbox" switch>'/, "")), "no innerHTML from data");
    assert.ok(!/\beval\(|new Function\(|document\.write\(/.test(script));
    assert.match(PAGE_HTML, /Content-Security-Policy/);
    assert.match(PAGE_HTML, /default-src 'none'/);
    assert.ok(!/<script[^>]+src=/.test(PAGE_HTML), "no outside script");
    assert.ok(!/https?:\/\/(?!sculptforge\.app|www\.w3\.org)[^"' )]*\.(js|css)/.test(PAGE_HTML));
    // Desktop-sent text goes in with textContent, and the secrets leave the address bar at once.
    assert.match(script, /textContent = st\.tool/);
    assert.match(script, /history\.replaceState/);
});

test("the Deck page seals commands for the relay with a direction and a rising sequence number", () => {
    assert.match(PAGE_HTML, /d: "p2d"/);
    assert.match(PAGE_HTML, /env\.d === "d2p" && env\.s > downSeq/);
    assert.match(PAGE_HTML, /Math\.max\(upSeq \+ 1, Date\.now\(\)\)/);
});
