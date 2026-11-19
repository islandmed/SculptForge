---
whats_new: **AI tools**: connecting to a live SculptForge window no longer shows as failed in clients such as Codex, so the sculpting tools appear right after you approve.
fixed: **Live mode** (`sculptforge-cli --mcp --attach`): a successful `live_connect` was reported to the AI tool as an error, so some clients (Codex) kept listing only `live_connect`. The reply now reflects the connection after it is made, and the tool-list-changed notice is sent after that reply. Reported by a community member.
testers:
  - Windows, Codex desktop with SculptForge attached: turn on Preferences > AI > Let AI tools control this window, ask the AI to connect, click Approve; the full tool set (scene_overview, viewport_image, ...) is listed and live_connect is not shown as failed.
---
