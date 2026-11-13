---
whats_new: **Image to 3D no longer freezes the window** when an AI tool (or the console or a script) runs it: it works in the background, shows its progress in the status line, and the model replaces the scene when it arrives.
fixed: **Image to 3D / Text to 3D from an AI tool, the console or a script** no longer froze the whole window for the 30 seconds to several minutes a hosted model takes, with no sign of progress (it looked like a crash, and force-quitting left no trace). The call now runs in the background with live status; the result opens when it arrives, and if the scene changed meanwhile nothing is replaced.
fixed: **Free Hugging Face models** that are down, asleep or not answering no longer wait up to 15 minutes: after 4 minutes queued (or 7 minutes running) it says which model isn't answering and the next free Hunyuan3D model runs instead, with a note.
changed: The crash log now notes a session that was ended from outside (Force Quit, the out-of-memory killer, a closed session) and records the terminal, quit and CPU-limit signals; a closed pipe can no longer end the app.
testers:
  - Connect an AI tool (Help > Connect an AI tool), ask it to run image_to_3d on a picture: the window stays alive, the status line counts up, and the model replaces the scene.
  - Linux: start the app from a terminal, run image_to_3d on a picture, and drag a window over it while it works: it keeps redrawing.
---
Reported by the owner (11-13): the app seemed to die during image_to_3d over MCP. Evidence: the command ran synchronously on the UI thread
(27 s in the second run, no status, nothing logged until the end); no core dump, no crash line, no audit entry for the first run = ended from outside.
