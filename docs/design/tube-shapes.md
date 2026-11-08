# Tube: point spacing, profiles, subtract, quick volumes

Requested by GABO on Discord (11-08), who uses Tube as a free 3D drawing tool. Built in slices; 1 and 2 ship first.

## Slice 1: fewer points

- **Point spacing** (Tool > Tube > Stroke): how far apart a drawn stroke's control points are, in radii (0.5 to 8,
  default 1.25: the old fixed value). `tubeFromStroke(..., spacing)`; `tube_new spacing=`.
- **Simplify** (Edit): removes about a third of the active Tube Part's points per press, always the point whose
  removal changes the curve least (distance from its neighbours' chord plus its radius change, in its own radius),
  ends kept, never below 2. One undo step. `tube_simplify keep=0.66 tolerance=`: `tolerance` (in radii) stops early
  so a press never bends the curve more than that.

## Slice 2: profiles and taper

Stored in `TubeShape` (and the project's `tube` JSON), so they stay editable until Bake:

- `profile`: round (the default, sweeps exactly as before), square, triangle, ribbon. Each is a closed outline in
  the ring's plane with "radius" = half its width; rings place their vertices along it by arc length. Square and
  triangle rings get a multiple of 4 / 6 sides so the corners stay sharp. Ribbon is a flat stadium,
  `thickness` (0.05 to 1, default 0.25) of its width.
- `roll` (degrees) turns the outline about the curve.
- `up`: the first ring's orientation. A drawn stroke stores the view direction, so a ribbon drawn on screen lies
  flat towards you. Unset, non-round outlines start from world Z (or Y), which the X mirror preserves.
- `taper_start`, `taper_end` (0 to 1): the fraction of the length over which the radius narrows to a point at
  that end (linear, to 3% of the radius; the cap closes it).
- Symmetry: the mirror copy is swept from the mirrored shape (positions, `up` mirrored, `roll` negated); every
  outline is symmetric across its own n axis, so the copy is the exact mirror.
- UI: Shape (Circle / Square / Triangle / Ribbon; "Round" is already the Deform verb in the translations), Thickness (ribbon), Turn, Taper start / end. They set new tubes
  and the active Tube Part; sliders preview live and apply as one undo step when let go (`tube_set`).

## Slice 3: Subtract

One tool: **Result** gets a third choice, **Subtract**. The stroke is swept watertight (even edges near the
active Part's) and cut from the active Part with the in-house boolean (as Cut uses), one undo step; Symmetry X cuts
the mirror copy too. Refused on a Part with subdivision or micro levels (as Merge is).

## Slice 4: quick blocking volumes

Forge Spheres build a chain; Insert places library meshes. Missing: a one-click ball. Tube already makes a ball from a
dab (a stroke shorter than its radius), so **a click with Draw Tube on drops a sphere** of the brush size, at the
surface depth; with Square profile a rounded cube. Merge / Subtract / New Part apply to it as to any stroke.
