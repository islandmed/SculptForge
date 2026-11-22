---
whats_new: **Remesh after each stroke** now says why it did nothing: on a Part with subdivision levels, sculpt layers or a painted texture a note over the view names the reason and the key that remeshes it anyway.
changed: **Remesh after each stroke** (Tool > Remesh) tells you, once per Part and reason, when a stretching stroke is skipped because the Part has subdivision levels, sculpt layers or a painted texture (a remesh would remove or redo them), and names the key (Shift+R) that remeshes it anyway.
testers:
  - Any OS: Tool > Remesh, tick Quick Remesh and Remesh after each stroke; add a subdivision level; pull a long Move stroke. A note over the view says the stroke was skipped and that Shift+R remeshes it anyway; the mesh is untouched and the same Part does not repeat the note.
---
Skip itself is unchanged (App::strokeEndedForRemesh); only the silence is fixed. Strokes recorded into a sculpt layer are still silent.
