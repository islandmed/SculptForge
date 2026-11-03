# Dynamic topology

With **Dynamic** on, the brush adds triangles where it needs detail and removes them where it doesn't, as
ZBrush's **Sculptris Pro** and Blender's **Dyntopo** do. Pull a sphere out into horns, ears or fingers and the
new shapes get triangles of their own instead of stretched ones; block in a creature from a ball without
remeshing first.

![A sphere with horns pulled out by Tendril with Dynamic on, as wireframe and shaded](images/dynamic-topology.jpg)

## Turn it on

| Control | Where | What it does |
|---|---|---|
| **Dynamic** | Top shelf, **Sculpt > Draw & Tablet** | Turns dynamic topology on or off (ZBrush's Sculptris Pro button) |
| **Detail** | Top shelf (while Dynamic is on), **Sculpt > Draw & Tablet** | How small the triangles get: 0 coarse, 100 fine (default 50) |
| **Constant detail** | **Sculpt > Draw & Tablet** | Off: the size follows the brush. On: one size for the whole mesh |

Then sculpt as usual. Every brush works with it except **Move** (it drags the surface without remeshing), the
mask and paint brushes, and the stamping strokes **Drag Stamp** and **Slide Stamp** (so VDM stamps don't remesh either). **Smooth** remeshes towards larger triangles, so smoothing
also simplifies, as in ZBrush.

> **Tip:** Turn on **Wire** (<kbd>Shift</kbd>+<kbd>F</kbd>) to see the triangles appear under the brush.

## How big the triangles get

By default the triangle size follows the brush size on screen, as in Sculptris Pro: **zoom in or make the brush
smaller for finer triangles**, zoom out or make it bigger for coarser ones. At Detail 50 an edge is about an
eighth of the brush radius; Detail 100 makes them about three times smaller, Detail 0 three times bigger.

With **Constant detail** on, every stroke aims for the same size whatever the zoom and brush, set by Detail
relative to the size of the mesh (at 50, about a hundredth of its bounding box diagonal). Use it to bring a
whole model to an even density, or to keep the density while you change brush sizes. It is a preference: Store
Config (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>) keeps it.

Under the brush, edges longer than the target are split, edges much shorter are merged away, and the new
triangles are evened out. Outside the brush nothing changes, so you can have big triangles on the back of a head
and fine ones around the eyes.

## Smooth remesh

Smoothing (<kbd>Shift</kbd>) with Dynamic on also simplifies by default: it remeshes toward triangles twice the
size, so the smoothed area gets coarser, as in Sculptris Pro. **Smooth remesh** under **Sculpt > Draw & Tablet > Dynamic topology**
sets how much (from the discussions):

- **Smooth only** (0): Shift only smooths; the triangles stay as they are.
- **Light** (50): it remeshes toward triangles one and a half times the size.
- **Simplify** (100, the default): as before.

It is a preference (Store Config keeps it) and is saved with the project; from a script,
`dyntopo smooth_remesh=0`.

## What it keeps and what it doesn't

- **Symmetry.** With X (or Y, Z) symmetry on, both sides are remeshed together and get mirrored triangles, not
  just mirrored shapes. A model whose sides aren't made of mirrored polygons (after Quad Remesh, or sculpted
  without symmetry) has no mirrored triangles to keep: each side then gets its own new triangles, as fine as
  the brush's side.
- **Mask and visibility.** Fully masked and hidden parts are left alone. Partly masked areas are remeshed but
  move less, as usual.
- **Borders.** Open edges and the borders between face groups stay where they are; their vertices only slide
  along them. New triangles take the face group of the triangle they came from.
- **Vertex Paint, mask values and sculpt layers** are carried onto new vertices (blended between their
  neighbours).
- **Quads** under the brush become triangles. The rest of the mesh keeps its quads.
- **UVs are dropped** the first time a stroke remeshes a Part (the status bar says so), since the new
  triangles have none. Undo brings them back. Unwrap again with **Auto UV** when you are done.

## Subdivision levels

Dynamic topology works on meshes without [subdivision levels](subdivision-levels.md), as in ZBrush. On a Part
with levels, **Dynamic** stays off and the status bar says why: go to the level you want to keep and use **Del
Lower** and **Del Higher** in **Tool > Geometry** first. If you turn it on and then pick a Part that has
levels, strokes sculpt it without remeshing.

A good workflow: block in with Dynamic, then [Quad Remesh](retopology.md) or [Voxel Remesh](geometry.md) for an even
base, **Divide** for levels, and sculpt the fine detail on them.

## Undo and speed

Each stroke is one undo step. Undo puts every triangle back exactly, and it only keeps the part of the mesh the
stroke changed, so it costs little memory even on millions of triangles (a few megabytes per stroke).

Dynamic topology is fastest on meshes of up to a few million triangles. On the benchmark computer (an M1 Pro), a
dab with a medium brush on a one-million-triangle sphere takes 9 to 13 milliseconds including the screen
update; undoing a stroke on such a mesh takes about a tenth of a second. A very big brush over very small
triangles merges them over several dabs rather than all at once, to keep the brush responsive. See
[Performance and big meshes](performance.md).

## From a script

```
dyntopo enabled=true detail=60
stroke points="0.3,0.6,1 0.5,0.4,1" brush=Tendril radius=0.2 symmetry=x
dyntopo enabled=false
```

`dyntopo` without arguments reports the settings; `constant=true` switches to a constant detail size. It works
the same from the console, `sculptforge-cli`, MCP and Python. See [Scripting and automation](scripting.md).

## See also

- [Sculpting and brushes](sculpting.md)
- [Subdivision levels](subdivision-levels.md)
- [Subdivide, remesh, decimate and LODs](geometry.md)
- [Coming from ZBrush](coming-from-zbrush.md)
