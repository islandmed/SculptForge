---
whats_new: **Polygon mode** (**Tool > Polygon**): select Points, Edges or Faces of a Part on the model, move them with the Gizmo and run polygon operations on them, each one undo step.
highlight: Polygon mode: model with points, edges and faces
added: **Polygon mode** (Tool > Polygon): the foundation for polygon modelling. Turn a Part into a cage (plain meshes up to
  2 million triangles; Parts with subdivision or Micro levels are told what to do first), pick Points, Edges or Faces and
  select them on the model (click, Shift adds, Ctrl removes, drag a box; double-click an edge for its loop, a face for its
  face group; Grow, Shrink, All, None, Invert, Loop, Ring, By Group), see the selection drawn over the model, move it with
  the Transform gizmo (W / E / R) as one undo step, and run operations on it from the panel, each one undo step: Delete
  and Flip ship with the mode, the modelling operations (Extrude, Inset, Bevel, Edge loop, Span) plug into the same
  registry. **Span** joins two edge loops or two face selections with new faces: two open borders get a band of quads
  (Segments adds rings across, Twist turns the pairing, Flip for a piece wound inside out), faces on two sides are
  removed and their openings joined (a tunnel through a block, a handle between two shells), and one whole border loop is
  closed across its width. Borders of different lengths pair by distance along each, with the odd triangles spread
  evenly; the band is a face group of its own and stays selected afterwards. Commands: `poly_mode`, `poly_select`,
  `poly_op` (`op=span segments=2 twist=0 flip=false`), and `transform selection=true`. Nothing changes for
  sculpting: the mode is off until you turn it on. Members' top wish (#225 and the ideas threads).
member_request: true
testers:
  - Any OS: Tool > Polygon > Polygon mode on a cube (Tool > Parts > Append > Cube). Click a face, Shift+click another, Ctrl+click
    it off, drag a box over a side. Switch to Edges and double-click an edge: its loop lights up. W, drag an arrow: only the
    selection moves; Ctrl+Z puts it back in one step.
  - Any OS: Delete on a few selected faces, then Ctrl+Z: the cube is whole again. Esc leaves the mode and the wireframe goes
    back to how it was.
  - Any OS: a Part with subdivision levels (Tool > Geometry > Add Level), then Polygon mode: the status line says to Delete
    Higher first, nothing else changes.
  - Any OS: Span on a cube: double-click a face (its whole side), orbit round, Shift+double-click the opposite side, Span:
    a tunnel through the block, one undo step, the new faces selected. Drag Segments while watching: the preview
    follows. Select only one side and Span: the status line says to pick two.
---
Foundation branch: the selection model, overlay, gizmo path and ops registry (src/core/PolyMode.h, src/core/PolyOps.h). The
op branches (poly-extrude, poly-inset, poly-bevel, poly-edge-loop, poly-bridge) register one op each in
registerBuiltinPolyOps and extend this "added:" entry rather than adding their own "fixed:" lines.
