# Deform panel: a whole-Part deformation stack

Request: foirgairwolf on Discord (#ideas, 11-06): a full toolbox of whole-Part deformations with an axis per
operation, a live preview, and a list to repeat, apply and reset. Clean-room: generic geometry verbs, our own
grouping, labels, order, ranges and UI.

## Operations (core/Deform.h)

Every operation maps a point `q = p - pivot` to `q'`; a vertex takes `p + w (p' - p)` with `w = 1 - mask` (0 on
hidden vertices; open borders move, unlike the smoothing operations). The pivot is the Part's bounds centre, or
the gizmo's pivot when it was placed. `ext[a]` = the largest `|q[a]|` over the Part (never 0), so `u = q[a] / ext[a]`
runs over -1..1 whatever the pivot. **Soft** swaps the linear profile `s(u) = u` for `u (3 - u^2) / 2` (flat at the
ends): the effect concentrates in the middle and eases out.

| Group | Op | Amount | Math |
|---|---|---|---|
| Transform | **Move** | -100..100 (% of `ext`) | `q[a] += t ext[a]` on each chosen axis |
| | **Turn** | -180..180 deg | rotate about each chosen axis in turn, X then Y then Z |
| | **Scale** | -100..100 | chosen components times `2^(t)` (100 doubles, -100 halves) |
| | **Squash** | -100..100 | per chosen axis: `f = 2^(-t)` along it, `1/sqrt(f)` across it (volume kept) |
| Bend & Twist | **Bend** | -180..180 deg end to end | centreline angle `phi(y) = Phi/2 s(u)`, `p' = C(y) + x N(phi)`, `C = integral of T(phi)` (table of 2049 samples) |
| | **Twist** | -360..360 deg end to end | rotate the cross-section about the axis by `Phi/2 s(u)` |
| | **Taper** | -100..100 | cross-section times `max(0, 1 + t s(u))` |
| | **Shear** | -100..100 (100 = 45 deg) | `q += b t ext[a] s(u)` |
| Shape | **Round** | -100..100 | chosen components toward the sphere / cylinder of their mean radius |
| | **Box** | -100..100 | toward the cube / square of their mean max-norm |
| | **Flatten** | -100..100 (% of the depth) | cut the + side (- side for negative) at a plane moved in by `t`; Soft blends over a band |
| | **Sag** | -100..100 | along -axis by `t R/2 (r/R)^2`, `r` = distance from the axis line (outer parts droop) |
| | **Depth** | -90..90 | cross-section times `1 / (1 - t u / 2)` (a perspective) |
| Surface | Polish, Relax, Smooth, Balloon | as before | unchanged (they keep borders) |

Bend and Shear bend / shift toward a **Direction** (0..360 deg) in the plane across the axis; 0 is +Y for X and Z,
+X for Y. Axis-type operations (Turn, Squash, Bend, Twist, Taper, Shear, Flatten, Sag, Depth) run once per chosen
axis, X then Y then Z; vector-type ones (Move, Scale, Round, Box) take the chosen components together (Round on
XZ = a cylinder about Y). Axis set = any of X Y Z (7 combinations).

Slice 2 (0.11.14): Surface group extras (Noise, Contrast, Inflate), re-symmetrize (+ averaged), using the
topological mirror map.

## Stack and preview

The panel holds a list (op, amount, axes, direction, soft, on). The command `deform_stack ops="bend 40 y soft;
twist 90 y"` runs the enabled entries in order as **one** undo step; each op also has its own command
(`deform_bend amount=40 axes=y soft=true`). Preview = the stack really applied (one undo step, label
"Deform stack"); a change undoes that step (only while `undo.revision()` still matches, as Line Gradient does)
and applies again. Below 1M vertices this follows the slider while dragging; above, on release. **Apply** keeps
it (the step stays, the list clears); **Apply selected** keeps only the selected entry; **Reset** undoes the
preview and clears the list. Anything else that touches the undo stack makes the preview stale: it is then simply
kept, never undone behind the user's back.

Cost: pointwise ops are one parallel pass (poolFor) over the vertices; the apply goes through the Sculptor
(layers record it like a stroke). Gate: 10M-vertex Part apply < 1 s on the laptop (heavy.sh bench).

## Slice 3: Lattice (fenix on Discord, #ideas 1568540856477024347)

Clean-room: the Python sketch posted with the request is not opened or used. Classic free-form deformation
(Sederberg and Parry 1986).

1. **Generate**: a cage of (l+1)(m+1)(n+1) control points on the Part's bounds (padded 1%), resolution per axis
   (2-8, default 3). Each vertex gets its local (s, t, u) in 0..1 once, at Generate.
2. **Edit**: the control points are picked and moved with the gizmo (one or a box selection); the preview is the
   Bernstein sum `p = sum B_i^l(s) B_j^m(t) B_k^n(u) P_ijk` (trilinear for resolution 1), weighted by 1 - mask,
   recomputed in parallel per change (positions only, through the same undo-replace preview as the stack).
3. **Apply**: bake into the vertices as one undo step and drop the cage; **Cancel** drops it unchanged.
   Toolbar buttons (Generate Lattice, Apply Lattice) plus Tool > Deformation > Lattice; commands
   `lattice_generate res=3,3,3`, `lattice_move_points`, `lattice_apply`, `lattice_cancel`.

The What's New fragment for this slice credits fenix on Discord.
