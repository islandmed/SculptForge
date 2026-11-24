---
whats_new: **Polygon mode** (**Tool > Polygon**): select Points, Edges or Faces of a Part on the model, move them with the Gizmo and run polygon operations on them, each one undo step.
highlight: Polygon mode: model with points, edges and faces
added: **Polygon mode** (Tool > Polygon): the foundation for polygon modelling. Turn a Part into a cage (plain meshes up to
  2 million triangles; Parts with subdivision or Micro levels are told what to do first), pick Points, Edges or Faces and
  select them on the model (click, Shift adds, Ctrl removes, drag a box; double-click an edge for its loop, a face for its
  face group; Grow, Shrink, All, None, Invert, Loop, Ring, By Group), see the selection drawn over the model, move it with
  the Transform gizmo (W / E / R) as one undo step, and run operations on it from the panel, each one undo step: Delete
  and Flip ship with the mode, the modelling operations (Extrude, Inset, Bevel, Edge loop, Span) plug into the same
  registry. Commands: `poly_mode`, `poly_select`, `poly_op`, and `transform selection=true`. Nothing changes for
  sculpting: the mode is off until you turn it on. Members' top wish (#225 and the ideas threads).
  **Inset**: shrinks the selected faces inward by a Thickness and moves them in or out by a Depth (both as a fraction of
  the Part's size; a slider drag previews the result), the selection as one shape or Each face by itself. The rim of quads
  between the old border and the new one keeps the faces' groups (or joins one new Rim group), UVs stay continuous,
  creases and UV seams stay on the old edge, and the thickness is held below the point where a face would turn inside out.
member_request: true
testers:
  - Any OS: Tool > Polygon > Polygon mode on a cube (Tool > Parts > Append > Cube). Click a face, Shift+click another, Ctrl+click
    it off, drag a box over a side. Switch to Edges and double-click an edge: its loop lights up. W, drag an arrow: only the
    selection moves; Ctrl+Z puts it back in one step.
  - Any OS: Delete on a few selected faces, then Ctrl+Z: the cube is whole again. Esc leaves the mode and the wireframe goes
    back to how it was.
  - Any OS: a Part with subdivision levels (Tool > Geometry > Add Level), then Polygon mode: the status line says to Delete
    Higher first, nothing else changes.
  - Any OS: Polygon mode on a cube, double-click a side (its face group), Inset with Thickness 0.1 and Depth -0.05: a sunken
    panel with a quad rim, no gaps, one Ctrl+Z puts the cube back. Then All, Each face on, Inset: every face shrinks evenly,
    none turns inside out even at Thickness 0.5.
  - Any OS: paint a texture, then Inset a few faces: the paint stays where it was on the inner faces and the rim.
---
Foundation branch: the selection model, overlay, gizmo path and ops registry (src/core/PolyMode.h, src/core/PolyOps.h). The
op branches (poly-extrude, poly-inset, poly-bevel, poly-edge-loop, poly-bridge) register one op each in
registerBuiltinPolyOps and extend this "added:" entry rather than adding their own "fixed:" lines.
