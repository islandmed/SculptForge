---
whats_new: **Center on origin, Drop to floor**: two buttons in the Gizmo panel put the Part's bounding-box centre on 0,0,0 or rest it on the floor, in one undo step (all Parts together with **All Parts**). With the Classic sculpting keys, **F** frames the active Part and **F** twice quickly frames the whole scene.
added: **Origin tools** in the Gizmo panel (requested by Ziel, with Devon's Origin tools idea): **Center on origin** moves the Part's bounding-box centre to 0,0,0 and **Drop to floor** rests its lowest point on Y = 0, each one undo step per Part, with **All Parts** together. Also the commands `center_on_origin` and `drop_to_floor` for scripts and the AI assistant.
added: **Frame keys** (idea by Fenix): with the Classic sculpting keys and more than one Part, F frames the active Part and F twice within **Frame double-tap** (Preferences > Input, 400 ms, 0 = off) frames the whole scene. A scene with one Part frames as before. **Frame scene** is a bindable action.
testers:
  - Classic sculpting keys, two Parts: F frames the active Part; F F quickly frames both.
  - Gizmo (W): move a Part away, **Center on origin** puts it at the origin; Ctrl+Z puts it back; **Drop to floor** rests it on the grid.
---
Credits: Ziel (Center on origin), Fenix (frame keys), Devon (Origin tools).
