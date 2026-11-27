---
whats_new: "**See-through**: a slider under **Solo** and **X-Ray** (Parts panel, and View > Render) sets how much X-Ray lets through, from solid to a faint ghost. A reference image behind the model stays visible while you sculpt."
highlight: See-through strength for X-Ray
added: "**See-through** (Parts panel, View > Render): X-Ray's strength, 0-100% (default 70%, the look X-Ray always had). Display only: never the Part or an export; brushes, masks and picking work on the visible front surface. Saved with the project (the X-Ray state too). A lone Part now draws only its nearest back and front faces, so a dense Part reads as a clean ghost. The `xray` command takes `strength`."
member_request: true
testers:
  - "Add a reference image behind a Part (Tool > Reference), turn on X-Ray, drag See-through up: the image shows through, down: the Part is solid. Sculpt a few strokes: they land on the front surface."
  - "Save, reopen: the strength and X-Ray state are kept."
---
Display-only; X-Ray off draws exactly as before (no extra pass).
