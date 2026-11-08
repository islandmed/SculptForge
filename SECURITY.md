# Security policy

## Supported versions

Security fixes go into the latest release and the `main` branch.

## Reporting a vulnerability

Please **don't open a public issue** for security problems.

Report them privately with GitHub's **Security → Report a vulnerability** button on this repository
(private vulnerability reporting). Only the maintainers can see the report. Please include:

- what an attacker could do and how (steps or a proof-of-concept file)
- the version or commit, and your operating system

We aim to acknowledge reports within 7 days and to publish a fix and an advisory once a fix is
available. We're happy to credit you unless you'd rather stay anonymous.

## Scope notes

SculptForge opens files from untrusted sources (OBJ, FBX, glTF, PLY, USD/USDZ, images) and can talk to
network services you configure (AI providers, Hugging Face Spaces, the Unreal Editor). Crashes or memory
errors from malformed files, and anything that could leak API keys, are in scope.

Packs may hold sandboxed plugins (WebAssembly, run by wasmtime with no imports, fuel, memory and time limits; see
docs/design/plugins.md). A plugin that reaches anything beyond the mesh it is given (files, network, other memory of
the app), crashes or hangs the app, or changes the scene when its run failed, is in scope.

API keys you enter, and studio sign-in tokens, are stored in the operating system's keychain (macOS Keychain,
Windows Credential Manager, Linux Secret Service), never in settings files or logs. They are only ever sent to the
service they belong to. SculptForge opens no network listener for AI tools (MCP is stdio only) or for sign-in (the
device authorization grant). For IT reviews, see docs/guide/security.md.

Local listeners, each off by default and only while the user has it on:

- **AI live mode** (Preferences > AI): a Unix domain socket (0600, in a folder only the user can enter, peer user
  checked) or a named pipe whose DACL grants the current user only, remote clients rejected. No TCP or UDP port.
- **Maya Live link** (File > Maya > Live link, #42): the same kind of endpoint (`maya-link.sock` /
  `\\.\pipe\sculptforge-maya-link-<user>`), never a TCP or UDP port, so browsers, other users and other computers
  can't reach it. It asks with a warning every time it is turned on, is off at every start (never remembered as on)
  and shows "Live link on" in the top bar while it is. A random 128-bit pairing code must open each connection;
  one client at a time. It carries mesh data only (names, vertices, faces, UVs; no commands, paths or scripts) in
  length-prefixed frames with a 1 GiB cap and timeouts, parsed strictly in-house (`src/link/MayaLink.cpp`,
  fuzzed in `tests/test_maya_live.cpp`).
