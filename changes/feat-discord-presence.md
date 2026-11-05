---
whats_new: **Show SculptForge on Discord**: **Edit > Preferences > Integrations** lets your Discord friends see you're sculpting, with the elapsed time and, if you like, the monthly challenge's theme or the file name. Off by default.
highlight: Show SculptForge on Discord (opt-in)
added: **Show SculptForge on Discord** (Preferences > Integrations, off by default; idea by ebarranco): Discord shows
  SculptForge, its icon and the elapsed time; optionally "Monthly challenge: <theme>" (typed, 64 characters at most)
  and the file's name (never its folder). Nothing else is sent. It talks only to the Discord app on the same computer
  through Discord's local connection (no network connection, no port, no sign-in), sends its status and nothing
  else, and ignores whatever Discord sends back. See the guide's "Show SculptForge on Discord".
testers:
  - Any OS with the Discord desktop app running: Edit > Preferences > Integrations > Show SculptForge on Discord: within a few seconds your Discord profile shows SculptForge with an elapsed time. Uncheck it: the status goes away at once.
  - Check Show the monthly challenge, type a theme, press Enter: Discord shows "Monthly challenge: <theme>". Check Show the file name with a saved file open: its name (no folder) is added.
  - Quit SculptForge while it's on: the status clears. Restart: it comes back on its own (the setting is kept).
  - Start Discord after SculptForge: the status appears within about a minute.
  - Linux with Discord as a Flatpak or Snap: same as above.
---
Security review checklist: REPORT.md in the branch's worktree (feat/discord-presence).
