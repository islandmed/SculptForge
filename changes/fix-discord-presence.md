---
fixed: **Show SculptForge on Discord** stays up to date. Changes made in quick succession (opening files, typing the
  challenge) used to be dropped by Discord after the fifth in 20 seconds. Now the latest always arrives.
  After Discord restarts, your status is back within seconds instead of a minute.
fixed: Discord no longer shows SculptForge while it isn't running. Runs by scripts and test tools (benchmarks,
  offscreen checks) never show it, whatever the preferences say. On Linux, a process SculptForge starts (an AI
  worker) can no longer keep the connection, and the status, open after SculptForge quits.
changed: **Show SculptForge on Discord** shows the new SF icon.
testers:
  - Windows / macOS / Linux with the Discord desktop app: Edit > Preferences > Integrations > Show SculptForge on
    Discord on. Your profile shows "Playing SculptForge" with the orange SF icon and the elapsed time.
  - With Show the file name on, open three files one after another quickly: within about 4 seconds Discord shows
    the last one.
  - Quit Discord and start it again while SculptForge runs: the status comes back within about 10 seconds.
  - Quit SculptForge, or end it from the Task Manager / kill it: the status disappears.
---
Reviewer notes:
- Tony 11-05: "doesn't update anymore, it shows while I don't have SculptForge running, and it needs the new icon".
- Agent runs: discord::agentRun (SDL_APP_ID=sculptforge-agent, SDL_VIDEO_DRIVER=offscreen or the offscreen driver,
  SF_NO_DISCORD). These come on top of supportCardTestMode (benchmarks, UI tests, scripts, renders, demos, CI).
  Agents run the app with Tony's prefs, where presence is on.
- Icon: large_image is https://sculptforge.app/press/sculptforge-icon-1024.png. Discord's docs allow an external URL
  in large_image (via the media proxy). type 0 (Playing) is set explicitly. The portal's "logo" asset was a
  placeholder cube. Not checked against a live Discord client (agents must not touch Tony's Discord).
- Rate limit: one SET_ACTIVITY every 4 s at most, always the latest; a clear never waits. Reconnect back-off is 2 s,
  doubling to 60 s.
- SOCK_CLOEXEC on the socket (Linux). Windows pipe handles were already not inheritable.
- macOS has no SOCK_CLOEXEC, so a fork in the instant between socket() and fcntl() could still leak the socket
  there.
- Tests: test_discord_presence covers the JSON shape, agentRun, reconnect after Discord restarts plus a later
  change, bursts sending the latest, and SIGKILL closing the connection with a child still alive.
