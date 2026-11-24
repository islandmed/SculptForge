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
  registry. **Extrude**: the selected faces move out along their normals (in, with a negative Distance) and get quad
  side walls in a face group of their own, as one connected region or face by face (Individual) with a groove between
  neighbours; Even thickness moves every face the full distance where the selection bends, Keep shape moves a region
  along one direction, Inset narrows the cap first; in Edges, selected open border edges extrude into a strip that
  extends the surface (or stands on it). The slider shows a live preview. **Span** joins two edge loops or two face selections with new faces: two open borders get a band of quads
  (Segments adds rings across, Twist turns the pairing, Reverse for a piece wound inside out), faces on two sides are
  removed and their openings joined (a tunnel through a block, a handle between two shells), and one whole border loop is
  closed across its width. Borders of different lengths pair by distance along each, with the odd triangles spread
  evenly; the band is a face group of its own and stays selected afterwards. **Edge loop**: new loops of edges across the ring of quads through the selected edge (or
  every ring the selected edges are on), Loops per ring evenly spaced, Position across the quads with a live preview; the
  loop runs round the model or stops at a border, a triangle (split in two) or a quad it already crossed, keeps face groups,
  UVs, seams, creases, paint and mask, and the new loops stay selected. **Bevel**: selected edges (or the edges of selected faces) become a strip of 1 to 12 faces across, shaped by a
  profile (flat chamfer, round, square-ish or concave); edges meeting at a vertex join in a mitre or a rounded corner
  (a box corner becomes a sphere piece), a bevel ending at a face cuts that face's corner round, and two bevels running
  through a vertex join into one strip. Selected points are cut off instead, flat or as a rounded cap. The width is a
  share of the Part's size and is held back so no two cuts meet (Limit width). Faces keep their groups, UVs, mask and
  paint; the bevel faces get a face group of their own. Open and non-manifold edges are left alone and counted. **Inset**: shrinks the selected faces inward by a Thickness and moves them in or out by a Depth (both as a fraction of
  the Part's size; a slider drag previews the result), the selection as one shape or Each face by itself. The rim of quads
  between the old border and the new one keeps the faces' groups (or joins one new Rim group), UVs stay continuous,
  creases and UV seams stay on the old edge, and the thickness is held below the point where a face would turn inside out. Commands: `poly_mode`, `poly_select`,
  `poly_op`, and `transform selection=true` (`edge_loop` and `edge_select op=ring` do the same from Edges mode for scripts). Nothing changes for sculpting: the mode is off until you turn it on.
  Members' top wish (#225 and the ideas threads).
member_request: true
testers:
  - Any OS: Tool > Polygon > Polygon mode on a cube (Tool > Parts > Append > Cube). Click a face, Shift+click another, Ctrl+click
    it off, drag a box over a side. Switch to Edges and double-click an edge: its loop lights up. W, drag an arrow: only the
    selection moves; Ctrl+Z puts it back in one step.
  - Any OS: Delete on a few selected faces, then Ctrl+Z: the cube is whole again. Esc leaves the mode and the wireframe goes
    back to how it was.
  - Any OS: a Part with subdivision levels (Tool > Geometry > Add Level), then Polygon mode: the status line says to Delete
    Higher first, nothing else changes.
  - Any OS: Polygon mode on a cube, double-click a side (its face group), Extrude: the side stands out as a block with quad
    walls; drag Distance below zero and it sinks in; Individual gives four separate blocks; Ctrl+Z takes each back in one step.
  - Any OS: Delete a side, switch to Edges, All, Extrude: only the hole's border edges grow a strip; the rest are left alone.
  - Any OS: Span on a cube: double-click a face (its whole side), orbit round, Shift+double-click the opposite side, Span:
    a tunnel through the block, one undo step, the new faces selected. Drag Segments while watching: the preview
    follows. Select only one side and Span: the status line says to pick two.
  - Any OS: Polygon mode on a cube, Edges, click one edge, Ring (the ring goes round the cube), Loops 3, drag Position (the
    three new loops preview as you drag), Edge loop: they are inserted and selected; Ctrl+Z takes them back in one step.
  - Any OS: a cylinder (Tool > Parts > Append), Edges, click a rim edge, Edge loop: the loop runs down the side and splits the
    two cap triangles it reaches, no hole.
  - Any OS: Polygon mode on a box, Edges, All, Bevel with Width 0.05 and Segments 4: a rounded box with sphere corners and no
    cracks (Tool > Geometry > Find Problems is clean); Ctrl+Z takes it back in one step. Drag Width with one edge
    selected: the preview follows; 1 segment is a flat chamfer whose ends cut the neighbouring faces' corners.
  - Any OS: Polygon mode on a cube, double-click a side (its face group), Inset with Thickness 0.1 and Depth -0.05: a sunken
    panel with a quad rim, no gaps, one Ctrl+Z puts the cube back. Then All, Each face on, Inset: every face shrinks evenly,
    none turns inside out even at Thickness 0.5.
  - Any OS: paint a texture, then Inset a few faces: the paint stays where it was on the inner faces and the rim.
---
Foundation branch: the selection model, overlay, gizmo path and ops registry (src/core/PolyMode.h, src/core/PolyOps.h). The
op branches (poly-extrude, poly-inset, poly-bevel, poly-edge-loop, poly-bridge) register one op each in
registerBuiltinPolyOps and extend this "added:" entry rather than adding their own "fixed:" lines.
