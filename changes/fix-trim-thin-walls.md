---
whats_new: **Trim** no longer tears holes in thin walls: on a shell or a thin wing it trims the side you see and stops short of the wall behind it.
fixed: "**Trim** on a thin Part (a shell, a wing, a cloth-thin sheet): the wall behind the one you trim stayed in the brush's reach, so the plane ran through the middle of the wall and stroke after stroke sank the near side through the far one, leaving holes. Trim now takes its plane from the side facing you, leaves what faces away (the inner face of a shell, an underside) alone, and stops a fraction of the brush size (2%) short of the wall below, so a thin wall is thinned, never torn. Front faces only on or off."
testers:
  - Any OS: Make Shell on a sphere with a thin Thickness (0.01); Trim across it with a big brush (Size about a fifth of the shell), several strokes over the same place, then orbit inside. The far wall is untouched and nothing is torn.
  - Same on a thin wing or a folded sheet, with Front faces only on and off.
---
Invariants: tests/test_trim_thin_walls.cpp (shell and curved wing, three sizes, front faces only on and off, three rounds of overlapping strokes: the walls never meet, the far wall does not move, nothing flips). The cut is clamped per vertex by a ray along the cut direction on the BVH the previous dab refit (Sculpt.cpp keepTrimWalls). Forge Ink (micro) Trim is a separate path, not changed here.
