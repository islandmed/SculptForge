---
whats_new: **Hidden selection in Polygon mode**: selected points, edges and faces on the far side of the model are drawn too, dimmer than the visible ones. **Tool > Polygon > Show hidden selection** turns it off.
added: **Show hidden selection** (Tool > Polygon): the selection on the far side of the surface is drawn dimmer and thinner
  than the visible part, so you can see what is selected without orbiting. The visible selection looks as before, unselected
  elements behind the surface stay hidden. On by default; saved in the preferences.
testers:
  - Tool > Polygon > Polygon mode > Edges; box-select the back edges of a box, then look from the front: the edges behind the surface show dimmer; Show hidden selection off hides them.
  - Points and Faces: the same. With X symmetry on, both sides show. Extrude / Bevel previews and the light and dark themes read clearly.
---
Reported on Discord (#bug-reports) for 0.14.2. Draw-list change only: no GPU state, no per-frame cost beyond the existing cap.
