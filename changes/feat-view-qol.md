---
whats_new: **Floor grid at Y=0** (**View > Display**, next to Floor grid): keep the grid at the world's height zero instead of under the model.
whats_new: **Centre view on the pointer**: <kbd>Alt</kbd>+middle click on the canvas slides the point under the pointer to the middle of the view, zoom and angle unchanged; the view then turns about it. Can take a key (Controls window, <kbd>F1</kbd>).
whats_new: **Numbered duplicates**: **Duplicate** names copies `Head.01`, `Head.02`, ... (a copy of a copy counts on), no more `copy copy`.
added: **Floor grid at Y=0** (View > Display and Tool > Viewport Look, next to Floor grid; requested by members): the floor
  grid stays at world height zero instead of following the lowest point of the visible Parts, a fixed reference while
  you move Parts up and down or work on a base. Off by default (the grid follows the model, as before); saved in the
  preferences.
added: **Centre view on the pointer** (#209, requested by members): Alt+middle click on the canvas (press and let go
  without dragging) slides the point under the pointer to the middle of the view, keeping the zoom and the angle; the
  view then orbits about that point. Over the background it centres on that spot. Works in every key style (an
  Alt+middle drag still pans, or orbits with Blender keys). The action can take a key in Preferences > Hotkeys and
  then works wherever the pointer is over the view.
changed: **Duplicate** numbers the copies (Discord ask): `Head` gives `Head.01`, then `Head.02`; duplicating a copy counts on
  from the same name (`Head.03`), the lowest free number first. Before: `Head copy`, `Head copy copy`.
testers:
  - Any OS: View > Floor grid on, then Floor grid at Y=0 on; Gizmo-move the model up and down: the grid stays put. Off again: it follows the lowest point. Restart: both switches are kept.
  - Mouse, every key style (Edit > Preferences > Input): Alt+middle click a spot on the model: it slides to the middle, same size; orbit afterwards turns about it. Alt+middle drag still pans (Blender keys: orbits) exactly as before.
  - Pen with a button set to Middle click: Alt + that button, tap without moving: centres; a drag pans.
  - Parts: rename a Part Head, Duplicate twice, Duplicate the first copy: Head.01, Head.02, Head.03; delete Head.02 and Duplicate again: Head.02.
member_request: true
---
Requesters (feedback/feature-queue.md): #209 centre on the pointer (not consented to a credit), numbered duplicates and the
Y=0 floor grid (Discord ideas, not consented): the notes say "requested by members" only. Classic defaults unchanged: both new
switches are off by default; the gesture is a click that had no meaning before in any key style.
