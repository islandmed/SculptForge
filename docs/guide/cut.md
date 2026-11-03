# Cut: line, lasso, rectangle and polyline

**Cut** cuts the active Part with a shape you draw on the screen. The shape is pushed straight into the view, so
what you see inside it is what gets cut, front and back. Every cut is one undo step, and a closed Part stays
closed (watertight), so you can keep sculpting, remeshing or 3D printing it.

## Pick a shape

Hold <kbd>Ctrl</kbd>+<kbd>Shift</kbd> and click the brush tile on the shelf (or press <kbd>B</kbd>) to pick a Cut
shape next to **Select Rectangle** and **Select Lasso**, or use the shape buttons at the top of **Tool > Cut**:

| Shape | How to draw it |
| --- | --- |
| **Cut Line** | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+drag a straight line. The shaded side is cut; hold <kbd>Alt</kbd> as you let go to cut the other side. |
| **Cut Lasso** | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+drag a free-hand outline. Its inside is cut. A lasso that crosses itself keeps its biggest loop. |
| **Cut Rectangle** | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+drag a rectangle. Its inside is cut. |
| **Cut Polyline** | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+click point after point. Click the first point again to cut the polygon; double-click or press <kbd>Enter</kbd> to cut along the open, bent line (the shaded side; <kbd>Alt</kbd> for the other); <kbd>Esc</kbd> drops it. |

While a Cut shape is picked, <kbd>Ctrl</kbd>+<kbd>Shift</kbd> cuts instead of hiding; pick **Select Rectangle**
or **Select Lasso** again to hide parts of the model.

## Pick a mode

**Tool > Cut** has three modes:

| Mode | What it does |
| --- | --- |
| **Away** | Removes everything the shape covers, through the whole Part, and closes the cut with a flat cap. A line slices the Part in two and keeps one side; a lasso or rectangle drawn inside the outline cuts a hole right through. |
| **In** | Cuts a flat-bottomed pocket where the shape covers the Part. **Depth** sets how deep, measured from the nearest point under the shape. |
| **Add** | Adds the shape, extruded towards you by **Depth** and joined to the Part (a boolean union): panels, ridges, buttons, sockets. |

**Depth** is a share of the Part's size (0.10 = a tenth of its largest side), so it means the same on a small or
large model.

## What the cut keeps

- The surface outside the cut is kept exactly, with its mask, vertex colours, UVs, face groups and sculpt layers;
  only the triangles the shape crosses are split.
- The cap (and the walls of a pocket or added shape) are a **face group of their own**, so you can select, mask or
  hide them in one click ([Face Groups](face-groups.md)).
- The caps are flat with long triangles. To sculpt on them, give the Part even topology first:
  **Tool > Remesh** or Dynamic topology ([Subdivide, remesh, decimate and LODs](geometry.md)).
- Only the active Part is cut; the other [Parts](parts.md) are untouched. Cut each Part on its own, or merge them
  first.
- With symmetry on (**X**, **Y**, **Z** or radial on the top shelf), the mirror image of the shape cuts too.

An open Part (a plane, a scan with holes) is trimmed without a cap. **Add** needs a closed Part. Forge Spheres
and Parts with a micro level must be made into a plain mesh first.

> **ZBrush:** this covers what the trim, clip and slice curve brushes and Live Boolean are used for, as one tool.
> SculptForge is not affiliated with Maxon or ZBrush.

## Scripting

The `cut` command takes the shape (`line`, `lasso`, `rect`, `polyline`), its `points` in normalised screen
coordinates, the `mode` (`away`, `in`, `add`), `depth`, `flip`, `closed` and an optional `view_proj` matrix; see
[Scripting](scripting.md).
