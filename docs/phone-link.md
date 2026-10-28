# Phone link: design

The phone becomes SculptForge's companion: capture (scan videos, photo bursts, one photo, splat `.ply` files), AR
on your desk, and a live face feed. No app, no account. User documentation: [docs/guide/phone-capture.md](guide/phone-capture.md).

Code: `src/link/PhoneLink.*` (session, routes, inbox), `src/link/HttpServer.*` (HTTP/1.1 server, parser, client),
`src/link/PhoneRtc.*` (WebRTC, AES-GCM), `src/link/FaceFeed.*` (face messages, takes), `src/link/Qr.*`,
`src/io/ArExport.*`, `src/app/PanelPhone.cpp` (window), `assets/phone/capture.html` (capture page, served by the
desktop), `website/link/index.html` (face page, on the website). Tests: `tests/test_phone_link.cpp`,
`tests/ui/phone_link.sfui`.

## The hard constraint: camera access needs HTTPS

Phone browsers give `getUserMedia` (live camera) only to secure contexts: HTTPS pages, or `http://localhost`.

- A page the desktop serves at `http://192.168.x.x:8765` is **not** secure. `<input type=file capture>` still works
  there (it opens the phone's own camera app and returns a video or photo file), which is all capture needs.
- An HTTPS page (our website on GitHub Pages) **can** use the camera, but it can't `fetch` or open a WebSocket to
  `http://192.168.x.x`: that's mixed content, blocked by every browser (and Chrome's Private/Local Network Access
  adds a prompt on top). A desktop certificate for a LAN IP isn't possible without a CA (self-signed ones show a
  full-page warning, and AR Quick Look / Scene Viewer then refuse the model URLs anyway).

### Options considered

| Option | Verdict |
|---|---|
| Self-signed HTTPS on the desktop | Needs a TLS server and a scary "not private" page on every phone; AR viewers refuse the model files. No. |
| Popup bridge: HTTPS page posts to an HTTP tab via `postMessage` | The camera tab goes to the background on phones (camera stops) or the relay tab is frozen. No. |
| Everything through a public relay (MQTT/WebSocket broker) | 30 messages a second of face data through someone's free service, internet latency, privacy. No. |
| `http://localhost` via `adb reverse` | Works (localhost is secure), but Android-only and needs USB + developer mode. Kept as a side door (`/face`). |
| **HTTP page for capture + HTTPS face page with a WebRTC data channel to the desktop** | **Chosen.** |

### The decision

1. **Capture and AR: an HTTP page served by the desktop** (`assets/phone/capture.html`, embedded in the app), LAN
   only, while the window is open. File inputs record video and photos; uploads are chunked and resumable; AR
   models are plain links for iPhone (`<a rel="ar">` for AR Quick Look), which work over HTTP. Android's Scene
   Viewer does not: it is a separate app that downloads the model itself and only accepts public HTTPS addresses,
   so on a plain-HTTP LAN address Chrome just downloads the file. Android AR therefore uses the website's AR page
   (`website/link/ar.html`): the same WebRTC link as the face page (`/api/face-link?mode=ar`, a reliable ordered
   channel), the GLB sent down it in 64 KB binary chunks, shown with WebXR by `<model-viewer>`. Chrome only:
   Firefox for Android has no WebXR, so the page offers to copy the link for Chrome (the offer stays unanswered).
2. **Live face: an HTTPS page on our website** (`website/link/index.html`, published by GitHub Pages at
   `https://sculptforge.app/link/`; base URL in `kPhoneLinkSiteUrl`, overridable with `SCULPTFORGE_LINK_URL`) running MediaPipe Face Landmarker on the phone,
   connected to the desktop by a **WebRTC data channel** (DTLS: WebRTC is allowed from HTTPS pages to LAN peers).
   Signaling needs no server of ours:
   - **desktop -> phone**: the capture page asks the desktop for a face link; the desktop creates the WebRTC offer
     and answers with `https://.../link/#v=1&k=<token>&d=<desktop>&o=<offer>&x=<key>&r=<relay topic>`. Everything
     after `#` stays on the phone: browsers never send the fragment to the website. The page removes it from the
     address bar at once.
   - **phone -> desktop**: the answer goes through a **relay**, **encrypted with AES-256-GCM** under the key from
     the fragment and carrying the session token, so the relay can neither read nor forge it. The link lists the
     relays in order (`&r=...&r=...`); the page posts to the first that takes it, and the desktop polls them all
     (once every 1.5 s, at most 5 minutes, only while a face link is waiting) with the HTTPS client it already has
     (`ai::httpRequest`). Default list (`kPhoneLinkRelayUrl`):
     1. **`https://link.sculptforge.app/relay/`**: our own Cloudflare Worker (`services/link-worker`, free plan, a
        Durable Object per topic, messages deleted after 10 minutes, at most 4 per topic, base64url only, browser
        posts only from our pages, no logs). Source and deploy steps in its README; until it's deployed the name
        doesn't resolve and both sides move straight on.
     2. **`https://ntfy.sh/`**: a free, public, open-source pub/sub relay (CORS open, no account), the fallback.
        The Worker speaks the same small part of its protocol (`POST /<topic>`, `GET /<topic>/json?poll=1&since=`).

     `SCULPTFORGE_LINK_RELAY` overrides the list (space-separated); `off` disables relays.
   - **No internet?** The face page offers **Connect without internet**: it opens the desktop's own
     `/rtc-answer?a=<answer>` page in a new tab (a top-level navigation, which mixed-content rules allow), which
     hands over the answer and closes itself.
   - Then frames flow phone -> desktop directly over the LAN: an unordered channel with no retransmits (a late
     frame is useless), negotiated as id 0 on both sides. No STUN/TURN: both devices share a network. The camera
     is started before the answer is made, so browsers include the phone's real host candidates.
3. **Desktop WebRTC: libdatachannel** (MPL-2.0, which allows combination with GPL-3.0 code; files are not marked
   "Incompatible With Secondary Licenses") with libjuice (MPL-2.0), usrsctp (BSD-3-Clause), plog (MIT) and **Mbed
   TLS 3.6** (Apache-2.0) for DTLS and AES-GCM. All static, fetched at configure time (`cmake/WebRTC.cmake`): about
   2.5 MB of binary, about a minute of extra build. Mbed TLS (not OpenSSL) because it builds from source the same
   way on Linux, macOS and Windows with no system package to ship. **Optional**: `-DSCULPTFORGE_WEBRTC=OFF`
   builds without it; the app then offers capture and AR, and says the face feed isn't available.

Verified here: the unit test runs a full WebRTC handshake between the desktop code and a second libdatachannel
peer (answer delivered through the encrypted relay path), another polls two relays (one down, one a fake), and
`services/link-worker` has its own tests (`node --test`, CTest `link_worker`). A real Chrome on this machine
connected to the desktop code with the face page's exact code path, its answer going through the real ntfy.sh,
frames arriving in about 3 s; and again through the Worker's code run locally (`test/local-server.mjs`) after a
dead first relay, with live MediaPipe frames. Not verified: iPhone Safari and Android Chrome (needs a real phone, see the hand tests).

## Pairing and security

- The server listens only while the window is open, on port 8765 (or the next free one), on all interfaces but
  answers **local-network clients only** (RFC 1918, loopback, link-local, 100.64/10): anything else gets 403.
- A **session token** (128 random bits) is in the QR code's URL; every API, model and face request needs it (query
  `k` or header `X-SF-Token`; compared in constant time). Only the page itself is served without it.
- A **6-digit pairing code** for typing the address by hand: 5 wrong codes change it; one try per 0.8 s.
- An unused token **expires** after 10 minutes (the QR code changes); every window opening makes a new one.
- **Limits**: 16 MB per request body (chunks are 4 MB), 4 GB per file, 8 GB and 500 files per capture, 8 open
  captures, 16 connections, 16 KB request heads, 64 headers; no chunked transfer encoding (no request smuggling).
- **Files**: names reduced to `[A-Za-z0-9._-]`, at most 80 characters, no leading dot, Windows device names
  prefixed; extensions checked per kind; written only into `Captures/.incoming/<id>/` and moved to the final
  folder when complete. Unfinished uploads are deleted when the window closes.
- The capture page has a strict Content-Security-Policy (no external resources at all).

## Protocol (capture page <-> desktop)

All JSON; `X-SF-Token` header (or `?k=`) on everything but `/` and `/api/pair`.

| Request | Purpose |
|---|---|
| `GET /` | the capture page |
| `POST /api/pair {"code":"123456"}` | -> `{"token"}` |
| `GET /api/status` | desktop name, version, limits, AR state, face state (also the heartbeat: every 2 s) |
| `POST /api/capture {"kind":"splat|photo|ply"}` | -> `{"id","chunk"}` |
| `POST /api/capture/<id>/file {"name","size"}` | -> `{"id","name","received":0}` |
| `PUT /api/file/<id>?offset=N` (bytes) | -> `{"received"}`; 409 `{"received"}` when a chunk is missing: resume there; overlapping resends add only the new part |
| `GET /api/file/<id>` | `{"received","size"}` |
| `POST /api/capture/<id>/finish` | moves it to `Captures/<yyyy-mm-dd_hhmmss>-<kind>/`, writes `capture.json`, runs the command; 409 lists incomplete files |
| `POST /api/capture/<id>/cancel` | drops it |
| `POST /api/ar {"scope":"subtool|scene","size":cm}` | asks for an AR model |
| `GET /ar/model.usdz`, `GET /ar/model.glb` | the newest model (503 while preparing) |
| `POST /api/face` | face messages (one per line), for `/face` on localhost and tests |
| `GET /api/face-link` | creates a WebRTC offer, -> `{"url"}` of the face page |
| `GET /rtc-answer?a=<answer>` | the no-internet way to hand over the answer |

The phone retries network failures with backoff (2 s doubling to 30 s, 12 tries) and resumes from what the desktop
has. Captures then run, by kind: `splat_train source=<video or folder>` (owned by the splat feature), the Image to 3D
window (generating at once, a window option), and `open` (splat `.ply`/`.splat`) or `subtool_append` (mesh `.ply`).
A missing command is reported in the status bar.

## Face feed

Message (`link/FaceFeed.h`): `{"type":"face","t":<ms>,"weights":{<ARKit name>:0..1},"head":[qx,qy,qz,qw]}`. MediaPipe's
blendshape categories use the ARKit names; it has no `tongueOut` and adds `_neutral` (dropped by the page, ignored by
the parser). The head quaternion is the rotation part of MediaPipe's facial transformation matrix. The desktop
clamps weights to 0..1, normalises the quaternion, rejects malformed messages, and calls
`face_weights json=<weights>` (owned by the facs feature) directly through the registry each frame: no console echo,
no undo step. A take is JSON Lines (`take.jsonl`, one message per line, phone clock) plus `capture.json`
(kind `face`, frames, seconds).

## AR export

`exportArModel` (`io/ArExport.h`): visible triangles only; each part reduced to its share of 80,000 triangles
(vertex clustering first above 3 M triangles, then quadric decimation); polypaint and textures baked into one
1024 px texture on new Auto UVs (AR Quick Look ignores vertex colours); centred, base at y = 0, scaled to the
requested size in metres; written with the regular GLB and USDZ exporters. Runs on a worker thread from a snapshot.
While the phone has viewed the model in the last 10 minutes, a change to the scene re-exports it (at most every
2.5 s, never mid-stroke); the page notices the new version and says so.

## What runs where

- The desktop part is plain C++ on every platform. Built and tested here on Linux (aarch64); the WebRTC
  dependencies build with MSVC and Apple clang in their own CI, but SculptForge's Windows and macOS CI haven't run
  this branch yet.
- MediaPipe runs on the phone (WebAssembly, GPU delegate with a CPU fallback; about 5 MB downloaded once from
  jsDelivr and Google's model storage). Any phone from the last five years manages 30 fps.
