# services

Small pieces that run on a server rather than on your computer. SculptForge itself has no servers of its own; this
is the one helper, and nothing in the app depends on it.

| Folder | What |
|---|---|
| [link-worker](link-worker/README.md) | A Cloudflare Worker that carries one encrypted message (the phone's WebRTC answer) back to the desktop while they pair. It cannot read or forge it. The desktop falls back to a public relay when it is down |

Design: [docs/phone-link.md](../docs/phone-link.md). The desktop side is in [src/link](../src/link/README.md).
