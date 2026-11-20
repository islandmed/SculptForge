---
whats_new: **AI tools**: AI clients that never refresh their tool list (ChatGPT) now see every SculptForge tool as soon as they connect, not only `live_connect`.
fixed: **Live mode** (`sculptforge-cli --mcp --attach`): `tools/list` offers the full tool set from the first call, before the connection is approved, because some clients (ChatGPT Work mode) keep the first list and ignore the list-changed notice. A tool called before `live_connect` is approved answers "Not connected to SculptForge yet: call live_connect (SculptForge must be open)"; after Approve the same tool works with no refresh. Reported by @maxyui77.
testers:
  - Windows, ChatGPT Work mode (or any client that caches tools/list) with SculptForge attached: the first tool list already shows scene_overview, viewport_image and the rest; ask the AI to sculpt, it calls live_connect, click Approve, and the tools work without restarting the client.
---
