# Cloth: Cloth brush, Drape and Garments

Two halves: the solver side (solver, collisions, fabric presets, Cloth brush, Drape) and the Garments side (Pattern
panels, the 2D Pattern window, triangulation, sewing, Convert to Part, SVG export). Names we use:
**Cloth brush**, **Drape**, **Garments**, **Pattern panels**, **Fabric** presets. Clean-room: our own design from
public research only (Provot 1995, Baraff & Witkin 1998, Mueller et al. PBD 2007, Macklin et al. XPBD 2016 and
"Small Steps" 2019, Bouaziz et al. projective dynamics 2014, Li et al. IPC 2020). No other cloth or garment app is
installed, run, read or named; no new dependencies.

## 1. Solver: XPBD, small steps (`src/core/Cloth.h`)

Particles at the mesh vertices (mass = fabric density x a third of the area of each triangle around it).
Per frame (1/60 s), `substeps` (default 12) of:

1. predict: `v += h (g + drag (wind - v))`, `p = x + h v` (pinned particles: inverse mass 0, or a pin
   constraint when the weight is between 0 and 1);
2. one constraint pass, **Jacobi with gathered corrections**: each particle sums the corrections its own
   constraints ask of it (`dlambda = -C / (w_a + w_b + alpha / h^2)`, lambda starts at 0 every substep, which is
   exactly XPBD with one iteration per substep), divided by its constraint count and relaxed (omega 1.5). No
   atomics, no graph colouring, the same result on any thread count (**determinism**);
3. contact: each particle within `thickness` of a collider (or inside it, by the outward normal) is projected out
   to `thickness`; tangential motion this substep is reduced by `friction` (Coulomb-like, position level);
4. `v = (p - x) / h`, `x = p`; damping once per frame.

Constraints:

| | C | compliance |
|---|---|---|
| stretch | `|xa - xb| - rest` for each edge | `stretchCompliance` when longer, `compressCompliance` (100-1000x softer) when shorter, so cloth buckles into folds instead of resisting like rubber |
| shear | covered by stretch on a triangle mesh (the triangle's three edges); a soft area term later if panels shear too freely | |
| bend | distance between the two far corners of each pair of triangles sharing an edge (Provot-style), rest from the rest shape | `bendCompliance` |
| seam | `|xa - xb|` (rest 0) for each `ClothSeam` | `seamCompliance` |
| pin | `|x - target|` weighted by the pin (1 = inverse mass 0) | |

Why XPBD and not projective dynamics or Baraff-Witkin: unconditionally stable at sculpting frame rates, compliance
in physical units (time-step independent stiffness), trivially parallel in the gathered form, and local (the Cloth
brush simulates only a region). Projective dynamics needs a global factorisation per topology (fine for Drape,
wasted for a brush); implicit Euler with Newton (Baraff-Witkin) is the reference we test energy against.

**Rest:** a frame's largest particle speed below `restSpeed` (1 cm/s) for `restFrames` (10) frames.

## 2. Collisions

Colliders are the other visible Parts, copied once into a triangle BVH (`ClothColliders`, built in parallel at
the start of a Drape; the brush uses the Part's own BVH and its other Parts). Contact is particle-vs-surface
closest point within `thickness + travel`, outward from the surface normal (inside-ness from the normal side, so
open meshes work). Self-collision: particle-particle on a hash grid (cells twice the reach, so each particle looks in
its 2x2x2 nearest cells), pairs found once a frame (each particle keeps its 8 nearest, ties by index; pairs close at
rest are skipped), pushed apart after each substep's constraints, Jacobi (same on any thread count). The Cloth brush
has it on (half an edge apart); an IPC-style barrier only if particle-particle proves leaky on coarse meshes.

## 3. Fabric presets

| Fabric | stretch | compress | bend | density kg/m^2 | friction | thickness |
|---|---|---|---|---|---|---|
| Silk | 1e-7 | 1e-4 | 5e-2 | 0.06 | 0.2 | 0.5 mm |
| Cotton | 1e-8 | 2e-5 | 5e-3 | 0.15 | 0.4 | 1 mm |
| Denim | 2e-9 | 5e-6 | 3e-4 | 0.40 | 0.6 | 2 mm |
| Leather | 1e-9 | 1e-6 | 2e-5 | 0.80 | 0.7 | 3 mm |

(Compliances in m/N per unit rest length; tuned on the drape-a-square-over-a-sphere test until each looks right
in screenshots: silk flows, denim holds wide folds, leather barely folds.)

## 4. Pins, gravity, wind

Pins from the **mask**: masked vertices (mask 1) are fixed, partly masked ones pulled softly. Gravity is world -Y
(9.81 m/s^2, a slider 0..2x); wind is a velocity with air drag. A SculptForge unit is a metre, so a sculpt modelled at
a real size drapes right; Drape has a **Scale** field (shows "body height 1.8 m") for sculpts made at another size.

## 5. Cloth brush (phase 1)

A stroke simulates only the vertices near it: the region grows along the stroke (vertices within 2.5 x the
radius of any dab), each with its stroke-start position as rest. Vertices under the brush are **driven** (moved by
the stroke like Move, weighted by the falloff); the region's outer ring is pinned, so nothing outside moves.
Each dab runs a few substeps on the region (stretch, compress, bend, gravity, contact with other Parts), so
dragging pulls the cloth along, the compressed side buckles into folds and the folds keep their length. Modes:
**Drag** (default), **Push** (along the normal, Alt pulls), **Settle** (gravity only: the region sags and drapes),
**Grab** (targets = each vertex's stroke-start position plus the brush's whole travel, weighted by its falloff around
the stroke's first dab: Move-like, held harder), **Expand** / **Pinch** (along the surface away from / toward the
centre; Alt swaps), **Inflate** (grows the rest lengths of the edges under the brush, `ClothSim::setRestScale`, and
lifts along the normals: the fabric balloons and wrinkles where it can't; Alt shrinks). **Soft body** (0..1): a weak
pin toward the stroke-start shape on every free vertex (0 = plastic, the default).
Undo = one stroke (the normal stroke record). Bars: stroke lag within the normal brush bars (region capped by the
tier's active-vertex budget, substeps reduced on big regions).

## 6. Drape (phase 2)

Part > Cloth: **Mark as cloth** (this Part simulates, the other visible Parts collide), Fabric, Simulate / Pause /
Reset, Steps until rest, pins from mask. Runs on a worker thread with a live preview; the result is the Part's new
positions (one undo step), a normal Part you can sculpt; thickness via the existing Extract / thicken.
Bar: a 20K-vertex shirt over a 2M-triangle body at rest in < 10 s on the laptop floor tier.

## 7. Interface with Garments (agreed 11-06)

Garments (`src/core/Garment.h`, `sf::garment`) builds its panels and calls the solver; the solver never knows
about panels.

```cpp
// src/core/Cloth.h (cloth owns)
struct ClothSeam { uint32_t a, b; };          // two particles sewn: rest length 0
struct ClothSetup {
    std::vector<glm::vec3> positions;         // where the user put the panels, world metres
    std::vector<glm::uvec3> triangles;        // all panels in one list (garments triangulates)
    std::vector<glm::vec3> rest;              // flat panels (any placement): stretch + bend rest = the 2D pattern
    std::vector<float> pin;                   // empty or per particle 0..1
    std::vector<ClothSeam> seams;             // not merged before the sim
    float seamCompliance = 0;                 // 0 = rigid once closed
};
ClothColliders colliders; colliders.add(bodyMesh); colliders.build();
DrapeResult r = drapeToRest(setup, colliders, fabricPreset(FabricPreset::Cotton), options, &cancel);
// r.positions: one per particle; r.rested, r.frames, r.milliseconds. options.progress(frame, maxSpeed) for a bar.
// For a live view: ClothSim sim; sim.init(setup, fabric); sim.setColliders(&colliders); while (sim.step()) draw(sim.positions());
```

Garments then welds each seam pair, keeps the panel layout as UVs and converts to a Part. Seams pull closed over
the first frames (the solver ramps their compliance from soft to `seamCompliance` over 30 frames so panels don't
snap). Garments' own `ClothInput { Mesh, seams, ... }` converts to `ClothSetup` on its side.

## 8. Threading, GPU, later

Gathered Jacobi is a flat loop per particle: `poolFor` now; the same kernel is a GPU compute shader later (one
thread per particle, no atomics). Collision queries are per particle and parallel. Wind
turbulence, sewing-line stiffening, animated bodies: later phases.

## 9. Patent questions for the owner (no conclusions)

- XPBD and PBD were published by researchers at a GPU vendor; are there patents on position-based dynamics
  methods we should check before shipping? (We implement only the published equations.)
- Pattern-panel garment simulation: answered by docs/design/garments.md "Do not build (patents)" (no automatic
  placement or sewing; the user places and sews every panel).
- Cloth brushes that simulate a local region under a sculpting brush exist in other sculpting tools; any patent
  on a "simulated region that follows a sculpt brush" we should search?

## 10. Tests

Solver unit tests (`tests/test_cloth.cpp`): a hanging sheet keeps its edge lengths (strain < 1%), energy goes down
with damping, a sheet dropped on a sphere rests above it (no particle closer than thickness) in a bounded number of
frames, the same result on 1 and N threads, seams close. Brush: a drag stroke on a plane makes folds (height
variance up) with edge lengths kept; nothing outside the region moves. UI tests and screenshots for the panel.
