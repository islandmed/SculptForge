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

API keys you enter, and studio sign-in tokens, are stored in the operating system's keychain (macOS Keychain,
Windows Credential Manager, Linux Secret Service), never in settings files or logs. They are only ever sent to the
service they belong to. SculptForge opens no network listener for AI tools (MCP is stdio only) or for sign-in (the
device authorization grant). For IT reviews, see docs/guide/security.md.
