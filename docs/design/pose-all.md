# Pose All Parts (Figure Pose): design

Request: @ABHITAR (Discord #ideas, 2026-11-06): pose every Part of a character together through one light mesh,
then carry the pose back onto the full-resolution Parts, their subdivision levels and micro levels. Today each
Part is posed on its own with the Gizmo and a mask (`core/MeshTransform.h`), which breaks down for a character made
of 20 Parts (body, armour plates, belts, eyes, teeth): every Part must be masked and rotated by hand about the
same joint, and a dense Part makes each drag slow.

Name: **Figure Pose** (the mode) and **Pose Mesh** (the light mesh). Commands `figure_pose_*`. No name another
app's posing feature or plugin uses; `tools/check_marks.py` now also rejects those names.

## 1. What the artist does

1. **Figure Pose** (Tools menu, or `figure_pose_begin`). The visible Parts are merged into one Pose Mesh of about
   300k triangles (setting), shown as a temporary Part; the other Parts are hidden until the pose ends. Each
   source Part is one face group on it, so Mask by Face group, Hide and Solo pick Parts.
2. **Pose it** with the tools that already work on any Part: the Gizmo (W/E/R) with a mask and its soft
   falloff ("mask the body, rotate the arm"), symmetry, the masking brushes and Mask by Face group. Drags are
   instant: the Pose Mesh is small. Undo and redo step through the pose as usual.
3. **Apply Pose** (`figure_pose_apply`): every source Part takes the pose at full resolution, as **one undo
   step** across all Parts. The Pose Mesh goes away and the Parts show again. **Cancel** (`figure_pose_cancel`)
   drops the Pose Mesh and changes nothing.

Later phases (§6): a line tool (drag a line along a limb, then rotate, bend or stretch about its end), rotate about
a picked joint centre, mirror pose, saved poses, and a posing skeleton.

## 2. The Pose Mesh (proxy build)

Per visible source Part with triangles (hidden Parts, the Extract preview and empty Parts are left out):

- **Source level.** A Part with subdivision levels starts from its lowest level that has the Part's share of
  triangles. Levels grow 4x, so that is 1-4 shares, and only a level past twice the share is decimated (a small
  one). Its shape is the light one posing wants, and the full resolution is never decimated. A Part without
  levels starts from its mesh.
- **Budget.** The total (default 300k triangles, 50k to 2M) is shared by triangle count with a floor of 2k per
  Part, so eyes and teeth keep their shape. A Part already within its share is copied as it is.
- **Decimate** with our quadric edge collapse (`core/Decimate.h`, Garland and Heckbert 1997), each Part on its own,
  in parallel. No voxel volume, no union of Parts, no quadrics carried between meshes (§7).
- **Merge** in Part list order (`mergeMeshes`) with face group = source Part, and keep, per source Part: its
  id, its vertex and triangle range in the Pose Mesh, its undo revision (to see edits made during the pose,
  which then refuse Apply for that Part) and the rest positions of its range.
- Layers, paint, UVs and textures are not carried: the Pose Mesh is shape only: one grey mesh whose face
  groups (shown with Groups) are the Parts.

## 3. Transfer back

### 3.1 What a pose is: a list of weighted transforms

Every Gizmo drag on the Pose Mesh is already one `TransformSession`: a transform (pivot, axes, translation,
rotation, scale) and a weight per vertex (1 - mask, eased by the falloff, 0 on hidden vertices), plus the
symmetry planes. The undo step of a drag on the Pose Mesh keeps that pair (`UndoRecord::transformOp`). So the
pose is exactly the Pose Mesh's undo history: the steps from the start of the pose up to the current one, in
order. Undo, redo and the Sculpt Timeline need no extra bookkeeping, and an undone drag is never applied.

Mask, hide and face-group steps on the Pose Mesh change no positions and are skipped. A step that moved
vertices any other way (a brush stroke) cannot be replayed in phase B: Apply says which step and offers to undo
back to it. Brushes on the Pose Mesh are phase C (§6).

### 3.2 Binding

For each source Part, every vertex of its working mesh gets the closest point on **that Part's own** rest-pose
Pose Mesh triangles (a teeth vertex never binds to the lips): a triangle and three barycentric weights
(`TriangleBVH::closestPoint`, read-only and thread-safe, in parallel). Binding is computed at Apply and
dropped per Part right after, so it never adds to the scene's memory: 16 bytes per vertex of one Part at a time.

### 3.3 Replay at full resolution

For each transform step k, in order, and each source Part:

    weight_k(v) = sum over the bound triangle's corners c of bary_c * weight_k(c)

then the same `TransformSession` runs on the Part's working mesh with weight_k, the step's transform and its
symmetry. Every full-resolution vertex is moved by the user's own transform at an interpolated weight: it is
not dragged along by the Pose Mesh's triangles. That is why it is smooth: the rotation is slerped per vertex
with a weight that is linear inside each Pose Mesh triangle and continuous across them, so a dense surface never
shows the Pose Mesh's facets, and a vertex far from the Pose Mesh surface (a deep crease, an overhang) turns
about the joint like its neighbours instead of being pushed along a normal.

What follows for free, through the existing data model:

- **Sculpt layers** move with the surface (base takes the transform, each layer's offsets the linear part),
  exactly as a Gizmo drag on Base mesh.
- **Higher subdivision levels** keep their detail in the local frame of the level below (`core/Multires.h`), so
  they turn with it when the level is next shown; lower levels are re-fitted by the usual lazy reconcile.
- **Micro levels** are scalar heights along the base surface's normal (`core/Micro.h`): they follow the base.
- **Topology, UVs, face groups, paint and textures** are untouched.

### 3.4 One undo step across Parts

Undo lives per Part today. Apply pushes one record per changed Part (the replayed sessions merged: for each
vertex its value before the first step that moved it), all carrying the same `UndoRecord::group` id. `undo` and
`redo` on a record with a group id also undo or redo the top step of every other Part whose top step has that
id; a Part that was edited since keeps its later edit, and the message says which Parts were left. The Pose
Mesh's own history goes with it.

## 4. Targets (floor tier, `SF_HW_SIMULATE=floor`; `bench.sh` style, slowest-frame ms)

Scene: 20 Parts, 50M triangles in total (about 25M vertices), no subdivision levels (the worst case).

| Step | Target | Measured, phase B (PC, 2 threads, 50M) | Notes |
|---|---|---|---|
| Begin (build the Pose Mesh) | < 60 s plain, < 2 s with levels | 66 s plain | parallel decimation per Part; with levels no decimation |
| Gizmo drag on a 300k Pose Mesh | slowest frame < 16 ms | 10 ms per move | one `TransformSession` on ~150k vertices |
| Apply | < 10 s, progress shown | 14.5 s (bind 10.3 s, replay 4.1 s) | binding + replay, parallel; 4 cores |
| Extra memory during the pose | < 200 MB | +181 MB | the Pose Mesh, its BVH, topology and rest positions |
| Peak extra memory at Apply | < 600 MB + the undo step | +915 MB, of which the step ~400 MB | binding of the largest Part, one at a time |
| Undo step size | 16 B per moved vertex (+16 B per sculpt layer) | 25M moved: ~400 MB | same as a Gizmo drag on every Part |

The first draft said Begin < 15 s plain and < 150 MB during the pose. Neither was measured; both were estimates and
both were wrong. Begin is decimation: about 0.1M triangles per second per thread on curved surfaces (1.3M to 20k
in about 10 s, the `decimate_valence_ab` entry), so 50M on 4 threads is about a minute. 15 s would need a new
decimator, and for that budget the right step is Begin from the lowest subdivision level. The Pose Mesh's own data
is ~180 MB at 300k triangles. Apply at 2 threads is 14.5 s, so 4 threads should land near the 10 s target; it is
measured on the laptop when it is free.

The `figure_pose_bench` entry (labels bench + nightly, `SF_FIGURE_POSE_MTRIS=2,20,50`) measures all of these on 20
flat cube Parts; flat surfaces are the worst case for decimation.

## 5. How it maps to UsdSkel (studio pipeline phase 1, §6 of studio-pipeline.md)

A weighted transform is a **one-joint skin**: the transform is a joint's pose (pivot = joint position, rotation =
its local rotation, translation and scale likewise) and the weight field is that joint's skin weights.

- **Same transfer path for a skeleton.** Phase 1's pose mode does linear-blend skinning of joints with weights;
  a skeleton can drive the Pose Mesh (weights painted or computed at Pose Mesh resolution, cheap) and the
  full-resolution Parts take it through the same binding: weights interpolated from the Pose Mesh, then LBS (or
  dual quaternions) per vertex. Weight painting and automatic weights run on 300k triangles instead of 50M.
- **Promote a pose to a skeleton.** The studio doc's "Pose by mask promoted to a one-joint skeleton" works on
  the whole figure: each transform step becomes a joint (parented in step order where pivots nest), its weights
  become `primvars:skel:jointWeights`, the pose becomes a `SkelAnimation` frame. Export writes UsdSkel with the
  bind pose = the pose before Apply.
- **Correctives.** Sculpting after Apply on a named pose is the studio doc's corrective layer: Δ_bind needs the
  inverse of each vertex's blended transform, which the binding plus the step list gives exactly.
- **Forge Spheres.** A Forge Sphere armature is already a joint tree with centres and radii: phase D can offer it
  as the posing skeleton (sphere centres = joints, weights from distance to the links), driving the same path.

## 6. Phases

- **A (this doc).** Design, patent check, names.
- **B (first working version).** `core/FigurePose.{h,cpp}`: begin, apply, cancel, info; Gizmo steps recorded
  in undo; replay transfer; grouped undo across Parts; tests (transfer of a rigid rotation is exact at the
  bound vertices; a soft-falloff rotation is smooth: no vertex's moved distance differs from its neighbours'
  by more than the Pose Mesh edge length times the weight slope; undo and redo restore every Part bit-exactly; a
  Part edited mid-pose is refused). App: a Tools menu entry, a status line ("Figure Pose: 20 Parts, 300k
  triangles; Apply / Cancel") and screenshots.
- **C.** Line tool (rotate / bend / stretch along a dragged line), rotate about a joint picked on the surface
  (centre of volume: midpoint of the ray's entry and exit), mirror pose. Brushes on the Pose Mesh are on the do-not-build list (§7).
- **D.** Posing skeleton (from Forge Spheres or placed joints), saved poses, UsdSkel export (studio phase 1).

## 7. Patent check (Google Patents, 2026-11-06) and risk flags

No patent from the makers of the leading sculpting app on posing through a light copy, or on pose transfer, was found. Related patents, with how this design stays clear:

1. **US 10,229,535 B2** (filed 2016-03-09, in force to about 2036): high-resolution elements located in a
   proxy mesh's coordinate system and moved with the deformed proxy, then a deformed high-resolution coordinate
   system. **Our transfer does not do that**: full-resolution vertices are moved by the user's transforms at
   interpolated weights (§3.3); the Pose Mesh's deformed triangles are never used to place them. **Flag:** a
   general "follow the deformed proxy surface" transfer (needed for brushes on the Pose Mesh, phase C) is close
   to its claims and needs the owner's OK and a claim reading first.
2. **US 8,345,044 B2** (filed 2009-11-04, to about 2029-2030): binding a high-resolution model to a low-resolution
   control cage through segmented thin layers, heat-diffusion weights and mean-value coordinates. **We use none
   of them**: closest point plus barycentric weights on the Part's own Pose Mesh. Do not add mean-value or
   harmonic cage coordinates before it expires.
3. **US 11,354,861 B2 / US 12,293,464 B2** (filed 2020 / 2022): a proxy for a mesh made of sub-meshes built
   from a voxel volume, choosing which sub-meshes enclose space. **We decimate each Part on its own**; never
   voxelise or union the Parts to build the Pose Mesh.
4. **US 12,293,467 B2** (2025): transferring quadrics from a reference mesh to a second mesh to make a proxy.
   **We run plain QEM on each Part's own mesh**; do not carry quadrics between meshes.

Public prior art the design rests on: QEM decimation (1997), linear-blend skinning (1980s games and film),
closest-point barycentric binding (wrap deformers since the 1990s), masked transforms with falloff (ours).
Animation-engine patents on converting mesh deformation to bones (US 7,859,538) are about export to a runtime and
not used. This is an engineering search, not a legal opinion: the owner decides on flag 1 before phase C.

### Do not build (patents)

Until the owner decides (coordinator, 2026-11-06):

- Brushes on the Pose Mesh, or any transfer that places full-resolution vertices by following the deformed Pose
  Mesh surface or frames (US 10,229,535). Phase C's brush item waits for this.
- Mean-value, harmonic or heat-diffusion cage binding, or segmented thin binding layers (US 8,345,044).
- A Pose Mesh built from a voxel volume or a union of Parts (US 11,354,861 / 12,293,464).
- Quadrics carried from one mesh to another when building the Pose Mesh (US 12,293,467).

## 8. Clean room

Built from this request and our own transform code only: no other app's documentation, files, names, UI
layout or behaviour details were used beyond the public one-line idea "pose a light merged copy, then transfer".
No step depends on vertex order matching another app's format.
