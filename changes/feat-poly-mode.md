---
whats_new: **Polygon mode** (**Tool > Polygon**): select Points, Edges or Faces of a Part on the model, move them with the Gizmo and run polygon operations on them, each one undo step.
whats_new: **Bevel** (Polygon mode): round or chamfer selected edges or points with a width, segments and a profile; the width is held back so cuts never cross.
highlight: Polygon mode: model with points, edges and faces
added: **Polygon mode** (Tool > Polygon): the foundation for polygon modelling. Turn a Part into a cage (plain meshes up to
  2 million triangles; Parts with subdivision or Micro levels are told what to do first), pick Points, Edges or Faces and
  select them on the model (click, Shift adds, Ctrl removes, drag a box; double-click an edge for its loop, a face for its
  face group; Grow, Shrink, All, None, Invert, Loop, Ring, By Group), see the selection drawn over the model, move it with
  the Transform gizmo (W / E / R) as one undo step, and run operations on it from the panel, each one undo step: Delete
  and Flip ship with the mode, the modelling operations (Extrude, Inset, Bevel, Edge loop, Span) plug into the same
  registry. Commands: `poly_mode`, `poly_select`, `poly_op`, and `transform selection=true`. Nothing changes for
  sculpting: the mode is off until you turn it on. Members' top wish (#225 and the ideas threads).
  **Bevel**: selected edges (or the edges of selected faces) become a strip of 1 to 12 faces across, shaped by a
  profile (flat chamfer, round, square-ish or concave); edges meeting at a vertex join in a mitre or a rounded corner
  (a box corner becomes a sphere piece), a bevel ending at a face cuts that face's corner round, and two bevels running
  through a vertex join into one strip. Selected points are cut off instead, flat or as a rounded cap. The width is a
  share of the Part's size and is held back so no two cuts meet (Limit width). Faces keep their groups, UVs, mask and
  paint; the bevel faces get a face group of their own. Open and non-manifold edges are left alone and counted.
member_request: true
testers:
  - Any OS: Tool > Polygon > Polygon mode on a cube (Tool > Parts > Append > Cube). Click a face, Shift+click another, Ctrl+click
    it off, drag a box over a side. Switch to Edges and double-click an edge: its loop lights up. W, drag an arrow: only the
    selection moves; Ctrl+Z puts it back in one step.
  - Any OS: Delete on a few selected faces, then Ctrl+Z: the cube is whole again. Esc leaves the mode and the wireframe goes
    back to how it was.
  - Any OS: a Part with subdivision levels (Tool > Geometry > Add Level), then Polygon mode: the status line says to Delete
    Higher first, nothing else changes.
  - Any OS: Polygon mode on a box, Edges, All, Bevel with Width 0.05 and Segments 4: a rounded box with sphere corners and no
    cracks (Tool > Geometry > Find Problems is clean); Ctrl+Z takes it back in one step. Drag Width with one edge
    selected: the preview follows; 1 segment is a flat chamfer whose ends cut the neighbouring faces' corners.
---
Foundation branch: the selection model, overlay, gizmo path and ops registry (src/core/PolyMode.h, src/core/PolyOps.h). The
op branches (poly-extrude, poly-inset, poly-bevel, poly-edge-loop, poly-bridge) register one op each in
registerBuiltinPolyOps and extend this "added:" entry rather than adding their own "fixed:" lines.
