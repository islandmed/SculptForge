---
whats_new: "**Clearer gizmo (All mode)**: a ring seen edge-on fades and is not pressed by mistake, a ring turned a little picks out at its rim past the arrows, and an arrow pointing at you fades. Idea by Fenix on Discord."
added: "**Clearer All-mode gizmo** (Transform > All): a ring within 15 degrees of edge-on (the front, side and top views) is drawn faint and can't be picked, so a press on the bar takes the arrow or ball there; between 15 and 35 degrees only the rim of the ring, past the arrow heads, picks; an arrow within 12 degrees of the view direction fades (a hollow marker) and can't be picked. Rotate about an edge-on axis with the Rotate tool or turn the view a little. Classic and Streamlined alike. Idea by Fenix on Discord."
testers:
  - "Any OS, Transform > All (Shift+W), front view (numpad 1), in Classic and in Streamlined: the two edge-on rings are faint; press along the red and green bars, the arrows and balls move/scale and no ring turns by accident."
  - "Turn the view about 25 degrees: the ring now lights up and turns only out past the arrow heads; mouse, pen and trackpad."
---
Reviewers: pick arithmetic in src/app/GizmoAll.h (ringPick, arrowFaded, kRimMin), drawing in Gizmo.cpp; tests test_gizmo_all, ui_gizmo_pick_clarity.
