---
whats_new: **Curves, part 2**: **Tool > Tube > Draw > Points** places a curve point by point; **Line: Straight** keeps the corners (a polyline) and **Smooth** curves through the same points, switch any time; **Shift+click** selects points and the Move / Scale / Rotate gizmo moves, turns and scales them; **Snap points to surface** lays a curve on the model.
highlight: Polyline curves, edited with the gizmo
added: **Standalone curves, part 2** (Tool > Tube): **Draw > Points** adds a point per click to the curve or tube being built (Enter, Next line or Draw Tube off ends the line; `tube_add_point`). **Line: Smooth / Straight** switches a curve or tube between a smooth curve through its points and a polyline with sharp corners, keeping the points (`tube_set straight=true`; Resample, Tube from curve and Repeat along curve follow it). **Shift+click** selects several points (Delete Point removes them all; `tube_delete_point indices=`). The **Transform gizmo** (W / E / R) moves, turns and scales the selected points, or the whole curve, re-sweeping the tube live, one undo step per drag (before, it moved the mesh and the points could no longer be edited). **Snap points to surface** puts the selected points on the nearest other Part (`tube_snap_points`), and with Snap to surfaces on a curve's dragged point lands on the model under it. Requested by Hameron and GABO.
member_request: true
testers:
  - Any OS: Tool > Tube > Result > Curve, Draw > Points, Line > Straight, Draw Tube; click five spots on and off the model, press Enter. Good = a polyline through the clicks; Smooth bends it through the same points, Straight brings the corners back.
  - Any OS: with the curve selected and Draw Tube off, Shift+click two dots (orange), press W and drag an arrow: both move, the rest stay; Ctrl+Z puts them back. Click one dot, press E and drag the centre: the tube gets thicker there.
  - Any OS: draw a curve in the air beside the model, press Snap points to surface: its points land on the model; Tube from curve and Repeat along curve work on it as before.
---
Part 2 of the curves ask (part 1 shipped in 0.12.3). Design: a whole-curve Smooth / Straight toggle rather than
per-point handles: the points are the data and both lines pass through them, so converting is lossless. Gizmo on a
single point: Move places it, Scale sets its radius, Rotate does nothing. The gizmo's "mesh moved, points no longer
fit" refusal still applies to Tube Parts whose mesh was changed some other way (Bake keeps them).
