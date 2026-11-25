---
whats_new: **Show SculptForge on Discord** (Edit > Preferences > Integrations) now shows one word for what you're doing, **Sculpting**, **Painting** or **Idle**, and nothing from your scene: the file name and monthly challenge options are gone.
changed: **Show SculptForge on Discord** (Preferences > Integrations, off by default): the status shows SculptForge, its icon, the
  elapsed time and one of three fixed words, Sculpting, Painting or Idle (no stroke for five minutes). The "Show the monthly
  challenge" and "Show the file name" options are gone: nothing typed and nothing from the scene is sent any more, and the
  preferences spell out what is shared. If you had one of them on, Preferences > Integrations tells you once; your on/off
  choice is kept.
security: **Show SculptForge on Discord** tries the Discord app again no sooner than 30 s after a failed or lost connection (was
  2 s), and switching it off ends its thread as well as clearing the status. Only fixed strings leave the app.
testers:
  - Any OS with the Discord app running: Edit > Preferences > Integrations > Show SculptForge on Discord. Your profile shows
    SculptForge with "Idle"; make a stroke: "Sculpting" within a few seconds; a Paint brush stroke: "Painting"; leave it five
    minutes: "Idle".
  - Uncheck it: the status clears at once. Quit Discord and start it again: SculptForge is back on your profile within about a minute.
  - Had "Show the file name" on in 0.14.0 or earlier: Preferences > Integrations shows a note once; Got it dismisses it for good.
---
Narrows the shipped feature to the owner's 11-23 conditions (brief: feat/discord-presence). Security checklist in REPORT.md.
