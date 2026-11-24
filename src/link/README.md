# src/link

Live connections between SculptForge and something else on the same computer or local network: an AI tool driving
the open window, a phone, Unreal Editor, Maya. These are the places SculptForge listens or connects, so every file
here is a security surface: the maintainers review every change to it closely.

The files are built into several libraries: the phone link is `sculptforge_phone`, live mode and the Maya link are in
`sculptforge_mcp`, and `Socket` and `UnrealLink` are in `sculptforge_ai`.

| Link | Files | How it talks |
|---|---|---|
| AI live mode | `AiLink`, `AiLinkSession` | `sculptforge-cli --mcp --attach` to the open app. A Unix domain socket (`ai-live.sock`, mode 0600, in a folder only you can enter) or a Windows named pipe (current user only, remote clients rejected). **No TCP or UDP port.** The peer must be the same user. Pairing needs an Approve click (or a one-time 6-digit code), then every call is sorted by policy into allowed, asked-first in the app, or refused. Newline-delimited JSON, lines capped |
| Maya live | `MayaLink` | The same kind of local endpoint, for the Maya shelf script ([integrations/maya](../../integrations/maya/README.md)). Opt-in, off at every start; the file exchange in [src/io](../io/README.md) is the base and works without it |
| Phone | `PhoneLink`, `HttpServer`, `PhoneRtc`, `FaceFeed`, `DeckCommands`, `Qr` | While the Capture from phone window is open, a small HTTP server on the local network serves the capture page, and takes uploads, AR model requests and the Forge Deck. The face feed travels over WebRTC. Design and threat model: [docs/phone-link.md](../../docs/phone-link.md) |
| Unreal | `UnrealLink`, `Socket` | "Send to Unreal": the exported FBX goes to a running Unreal Editor through its own Python remote execution. UDP multicast discovery then a TCP command channel; every call has a timeout |

## Rules for this folder

- **Nothing listens unless the user opened it,** and nothing keeps listening after the window or mode is closed.
- **Authenticate, then allowlist.** The phone needs the session token (in the QR code; it changes every time and
  expires if unused for 10 minutes); only local-network clients are answered. The Forge Deck accepts one command from
  a short allowlist (`DeckCommands`) with every value range-checked: the phone never names a command to run.
- **Hard limits** on head size, body size, header count, connections, line length and upload size; file names are
  reduced to safe characters and written only inside the session's own folders.
- **Parse as data.** A tool name is looked up and its arguments become typed values; nothing is evaluated.
- Studio mode (`core/NetworkPolicy.h`) restricts sockets to loopback unless the profile allows the local network.
- A new listener, port, pipe or message type needs a maintainer's approval before it merges. Say so in the pull request.

## Test it

`tests/test_ai_link.cpp` (every pairing and policy path, with no window or socket), `tests/test_phone_link.cpp`,
`tests/test_maya_live.cpp` and the UI scripts `tests/ui/ai_live.sfui`, `tests/ui/phone_link.sfui` and
`tests/ui/maya_live.sfui`. See [tests/README.md](../../tests/README.md) for how to run them.
