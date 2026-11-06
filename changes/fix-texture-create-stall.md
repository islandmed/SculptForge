---
whats_new: **Auto UV never freezes the app**: the `uv_auto` command from the console, a script or an AI tool now unwraps
  in the background like the **Tool > Texture Map > Auto UV** button, which gains a **Cancel** button beside its progress.
fixed: Auto UV from the console, a script or an AI tool (`uv_auto`) froze the app while it unwrapped: about 10 s on a
  327k-triangle sculpt. It now runs on the same worker as the Auto UV button, so frames keep drawing (longest frame
  under 100 ms, floor tier included). The next statement on the line, the AI tool's reply and a UI test's next step
  wait for the UVs, so `uv_auto; texture_create` still works in one go. Macros keep running it in place.
added: **Cancel** for Auto UV (Tool > Texture Map), beside its progress bar.
changed: The console keeps one line for a job's progress ("Auto UV: 42%") instead of one line per percent.
testers:
  - Any OS: open the console (backtick) and run `new_sphere subdivisions=7` (327k triangles), then
    `uv_auto; texture_create size=1024`. The view still orbits while it unwraps; the texture appears after.
  - Any OS: `new_sphere subdivisions=7`, then Tool > Texture Map > Auto UV, then Cancel: the progress bar goes away and the Part has no UVs.
---
Found by slowtests: its probe saw a 15 s frame "after texture_create", but SF_UI_PROFILE charged a run line's own time
to the next line. The frame was the synchronous `uv_auto` (xatlas) before it; texture_create itself costs < 15 ms.
The profiler now charges each step its own time (Demo::update).
