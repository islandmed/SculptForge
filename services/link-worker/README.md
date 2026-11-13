# SculptForge link relay (Cloudflare Worker)

The phone's face page (`website/link/`, HTTPS) and the desktop can't reach each other directly until their WebRTC
connection is set up: the desktop's offer travels to the phone in the face link's `#fragment`, but the phone's
answer has to get back somehow. This Worker carries that one message. It is a few kilobytes of AES-256-GCM
ciphertext; the key is in the fragment, which only the phone and the desktop have, so the relay can't read or
forge it. After that the face data goes straight over the local network. Design: [docs/phone-link.md](../../docs/phone-link.md).

It speaks the part of ntfy.sh's protocol the desktop already uses, so the desktop tries it first and falls back to
ntfy.sh (and the page does the same):

| Request | |
|---|---|
| `POST /relay/<topic>` | body: the message (base64url, at most 8 KB); `<topic>` is `sculptforge-` + 24 hex digits |
| `GET /relay/<topic>/json?poll=1&since=<unix seconds>` | one JSON object per line: `{"id","time","event":"message","topic","message"}` |

Each topic is a Durable Object (strongly consistent: KV could take up to a minute to show the phone's message to
a desktop that reaches another Cloudflare location). A topic keeps at most 4 messages, deleted 10 minutes after
the first. Browsers may post only from the pages in `ALLOWED_ORIGINS`. No logs are kept.

**Cost:** one face connection is one POST and up to 200 polls (one every 1.5 s, stopped as soon as the answer
arrives, at most 5 minutes). The free plan allows 100,000 Worker requests a day.

## Test

```sh
node --test test/            # unit tests, plain Node 20+, no packages
node test/local-server.mjs   # the relay on http://127.0.0.1:8787/relay/ (in-memory)
SCULPTFORGE_LINK_RELAY=http://127.0.0.1:8787/relay/ ../../build/sculptforge
```

CTest runs the unit tests too (`link_worker`) when Node is installed.

## Deploy (maintainers)

Needs the Cloudflare account that holds the `sculptforge.app` zone (free plan is enough).

```sh
cd services/link-worker
npx wrangler login
npx wrangler deploy          # creates link.sculptforge.app as the Worker's custom domain
curl https://link.sculptforge.app/   # "SculptForge link relay"
```

The desktop already lists `https://link.sculptforge.app/relay/` first in `kPhoneLinkRelayUrl`
(`src/link/PhoneLink.h`), so nothing else changes. To stop using ntfy.sh as the fallback once the Worker has
proved itself, remove it from that constant. `SCULPTFORGE_LINK_RELAY` (space-separated URLs, or `off`)
overrides the list.

If the face page moves to another domain, add it to `ALLOWED_ORIGINS` in `wrangler.toml` and deploy again.

## Forge Deck mailbox (`/deck/<topic>/up|down`)

For a phone whose browser can't reach the computer on the home network, the Forge Deck page's commands go up through
here and the computer's state comes down. Same topic names and the same rule as above: the relay carries ciphertext
only. Every message is an AES-256-GCM envelope `{"d":"p2d"|"d2p","s":sequence,"m":message}` under the Deck link's
key (only in the link's `#fragment`); the computer drops one with the wrong key or direction or a sequence number
that is not higher than the last, and everything it accepts goes through the same six-command allowlist as the
direct link.

| Request | |
|---|---|
| `POST /deck/<topic>/up` | the page (needs an `Origin` from `ALLOWED_ORIGINS`) posts one command; base64url, at most 1,400 bytes; 429 when 64 are waiting or more than 40 a second |
| `GET /deck/<topic>/up?after=<n>` | SculptForge (no `Origin`) collects commands numbered above `n`, oldest first; those up to `n` are deleted; a command nobody collects is dropped after 20 seconds |
| `POST /deck/<topic>/down` | SculptForge posts the state (at most 16 KB); the newest replaces the older |
| `GET /deck/<topic>/down?after=<n>` | the page reads the state if it is newer than `n` (`{"n":..,"message":..}`, else `{"n":..}`) |

The Deck's data is deleted 5 minutes after the last message. The `Origin` rules are hygiene, not security: anyone who
knows the (96-bit, random) topic can post, and what protects the computer is that nothing without the key decrypts.
**Cost:** a phone on the relay polls twice or three times a second while the Deck is in use (free plan: 100,000
requests a day, so a few hours of relay use a day); the computer polls only while the direct link is down.
