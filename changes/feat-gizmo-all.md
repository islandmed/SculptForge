---
whats_new: **Gizmo, All**: **Shift+W** (or **All** beside Rotate) puts move, rotate and scale handles on one gizmo: arrows and squares inside the rings, boxes past them to stretch, four outer tabs to scale evenly.
highlight: One gizmo with move, rotate and scale handles
added: **Gizmo All mode** (Shift+W, top shelf, Sculpt > Symmetry & Gizmo; the `gizmo_mode` command): move, rotate and
  scale handles at once, with clear priorities where they cross and no overlaps at any zoom or UI scale. Snapping,
  the readout, posing with a mask, Alt+drag for the pivot and one undo step per drag work as in the single modes.
  Idea by fenix on Discord (thanks @fenixpoints, #118).
testers:
  - Windows at 100% and 200% scaling: Shift+W on a model; hover every handle (each turns yellow on its own), drag an arrow, a ring, a box and an outer tab; each drag is one Ctrl+Z.
  - Any OS: mask half a model, Alt+click a joint, Shift+drag a ring in All mode: 15 degree steps, the masked half stays.
  - Any OS: orbit until an axis points at you: its box hides instead of sitting on the rings; orbit back, it returns.
---
Key: Shift+W, not T (T is the Brushes radial menu in every input style; tests/ui/radial_menu.sfui keeps it free).
