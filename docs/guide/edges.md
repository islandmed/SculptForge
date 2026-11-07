# Edges: selecting and creasing edges

**Tool > Geometry > Edges** selects edges of a Part's base mesh: one by one, as whole edge loops, along open
borders, or every hard edge at once by the angle between its faces. The selection is what edge creases are set
on, so a subdivided model keeps a sharp rim, a crisp panel line or a hard corner where you want one.

Edges are picked on the **base level** (SDiv 1), the cage the higher levels are made from. A quad's diagonal is no
edge: only the polygons' own sides are selected.

## Edges mode

Click **Edges mode** (the button again, or picking any brush, goes back to sculpting). A Part with subdivision
levels steps down to SDiv 1 first; stepping up again with <kbd>D</kbd> pauses the clicks until you are back on the
base.

| On the model | Does |
| --- | --- |
| Click | Select the edge under the cursor (only that one). The edge a click would take lights up as you move. |
| <kbd>Shift</kbd>+click | Add the edge. |
| <kbd>Ctrl</kbd>+click | Remove the edge (it lights up red). |
| Double-click | Select the edge loop through it: across the quads, on through every vertex with four edges, until it comes back round or meets a pole or a border. On an open border it follows the border. <kbd>Shift</kbd> / <kbd>Ctrl</kbd> add or remove the loop. |
| Click the background, <kbd>Alt</kbd>, right-drag | Navigate as usual. |

Selected edges are drawn in orange over the model, on the side facing you.

## Select by Angle

**Select by Angle** selects every edge where the faces on either side meet at more than **Angle** degrees (30 by
default; 0 is flat): the hard edges of a box, the rim of a cut, the ridge of a blade. Lower the angle to find softer
edges too.

With **Only within selection** it keeps just the sharp edges among those selected now: double-click a loop, then
Select by Angle to keep only its hard stretch.

## Grow, Shrink and the rest

- **Grow** adds every edge touching a selected one.
- **Shrink** drops the edges at the loose ends of the selection, so it takes back what Grow added to a loop; a
  closed loop keeps every edge.
- **Open Edges** adds the open (border) edges: the rims of holes and of open surfaces.
- **Invert** swaps selected and unselected edges; **Deselect All** clears the selection.

The line under the buttons counts the selection (and the creased edges): *12 edges selected of 1920*.

The selection belongs to the active Part. Picking another Part, or changing the mesh's polygons (remeshing,
Dynamic topology, Delete Hidden), drops it. It isn't part of undo, and it stays when you leave Edges mode.

## Creases

**Crease** gives the selected edges a crease of the **Crease** value: 1 is fully sharp, lower values are softer (0.5
is half as sharp). **Uncrease** removes it. Each is one undo step.

Creased edges are drawn in blue over the base level, brighter and thicker the sharper they are, also when you are
sculpting (not in Edges mode); selected creased edges are a thicker orange line. The line under the buttons counts
them: *24 edges selected of 48, 24 creased*.

Creases are saved in the project with the base level and stay there while you step up and down the levels.
Subdivision that keeps creased edges sharp comes in the next step of #108.

## Big meshes

Only the base level is ever searched: the levels above it never pay for edge lists. On a base of a million
vertices (two million edges) the edges are found once, in about a fifth of a second, when Edges mode starts; clicks
and double-click loops are instant (under a millisecond), and Select by Angle takes under a tenth of a second.

## Scripting

Every click and button runs the `edge_select` command, so the console and macros show it:

```
edge_select op=angle degrees=30
edge_select op=loop a=12 b=13 add=true
edge_select op=grow
edge_select op=info
```

`op` is `edge`, `loop`, `angle`, `open`, `grow`, `shrink`, `all`, `none`, `invert` or `info`. `edge` and `loop`
take an edge index (`edge=N`) or the two vertices it joins (`a=` `b=`).

Creases are the `edge_crease` command:

```
edge_crease value=1
edge_crease value=0.5
edge_crease clear=true
edge_crease clear=true all=true
edge_crease info=true
```

`value` (0..1, default 1) is set on the selected edges; `clear` removes their crease; `all` works on every creased
edge of the Part instead of the selection.

## See also

- [Subdivision levels](subdivision-levels.md)
- [Subdivide, remesh, decimate and LODs](geometry.md)
