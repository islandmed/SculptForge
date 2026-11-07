---
fixed: The window's size and place and project.json (recent files, Discord and autosave settings) are written to a
  temporary file and then renamed, so a quit that ends mid-write (a stalled disk) keeps the last whole copy.
testers:
  - Windows, real hardware: Preferences > Integrations > Show SculptForge on Discord on, sculpt a stroke, close the
    window. Within 15 s SculptForge.exe is gone from Task Manager and the Discord status is cleared. Start again:
    the window opens where it was, and Open Recent still lists your files.
---
Follow-up to fix/presence-on-exit (merge-0106-4b's review): the exit watchdog's _exit / TerminateProcess could land
mid-write on a stalled disk. ui.json already used writeJsonAtomic; window.json and project.json now do too.
