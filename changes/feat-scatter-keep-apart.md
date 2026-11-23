---
whats_new: **Scatter**: **Tool > Scatter** covers a Part, or its masked area, with copies of a pebble, a cube or any
  other Part, each at a random size and turn. **Keep apart** slides them over the surface until no two overlap.
highlight: Scatter copies over a surface, kept apart
added: **Scatter** (Tool > Scatter, the `scatter` command): copies of a built-in shape or another Part over the active
  Part or only its masked area. Count, random size range, random turn and tilt, follow the surface or stay upright,
  lie flat, height, seed. **Keep apart** relaxes the copies over the surface until none overlap or touch (Tight fit:
  their boxes; off: a round space each), with a **Spacing** gap; copies with no room are left out and the status line
  says how many. One joined Part (a face group per copy) or separate Parts; one undo step; changing a setting redoes
  the last scatter in place.
testers:
  - Any OS: Tool > Scatter on the default head, Piece Pebble, Count 1000, Keep apart on: press Scatter. The copies
    cover the head, none overlap (look close at a few clusters); Ctrl+Z removes them all at once.
  - Drag Count and Spacing with a scatter showing: it updates on the model; Keep it, then Scatter again adds a second one.
  - Mask the top of the head (Ctrl+drag), tick Only the masked area: copies only there, none past the mask's edge.
  - Windows, base-spec laptop: Count 10000 with a small Shape size, Keep apart on: note how long it takes (status line).
---
Keep apart: sphere or box (separating axis) pushes in the tangent plane, Jacobi passes in parallel, closest-point
reprojection onto a BVH of only the allowed triangles, greedy removal of whatever still touches (the invariant holds
by construction).
