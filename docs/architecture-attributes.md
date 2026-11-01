# Mesh attributes (v0.2 foundation)

`sf::Mesh` (src/core/Mesh.h) carries optional attributes, **empty until a feature needs them**
(no memory cost). When present, the size always matches the mesh.

| Attribute    | Type                      | Size           | Empty means          |
|--------------|---------------------------|----------------|----------------------|
| `mask`       | `std::vector<float>`      | vertices       | nothing masked       |
| `colors`     | `std::vector<glm::vec3>`  | vertices       | no vertex paint         |
| `polygroups` | `std::vector<uint32_t>`   | triangles      | every triangle in group 0 |
| `hidden`     | `std::vector<uint8_t>`    | triangles      | all visible          |
| `uvs`        | `std::vector<glm::vec2>`  | 3 x triangles  | no UVs (corner order = triangle order) |
| `quadPartners` | `std::vector<uint32_t>` | triangles      | no quads (`Mesh::kNoQuad` = a plain triangle) |

- Mask: 0 = free, 1 = fully masked. Colors: **linear** RGB 0..1 (OBJ/PLY convert from sRGB via
  `srgbToLinear`/`linearToSrgb`; glTF COLOR_0 is already linear).
- Read with `maskAt(v)`, `colorAt(v, fallback)`, `polygroupAt(t)`, `triangleHidden(t)`, `vertexHidden(v)`
  (all adjacent triangles hidden); safe on empty attributes. Write after `ensureMask()` /
  `ensureColors()` / `ensurePolygroups()` / `ensureHidden()`; `clearMask()` etc. free them again.
- `rebuildTopology()` drops wrongly sized attributes (`sanitizeAttributes()`): fill them first.

## Quads
Meshes are triangles, but `quadPartners[t]` names the triangle `t` forms a quad with (an imported quad split in
two, a Plane/Cube cell, a voxel-remesh cell). Pairs are mutual, share one edge wound oppositely, and
`sanitizeAttributes()` unpairs anything else, so a topology change can never leave a stale pair (at worst it
loses some). Record pairs with `pairQuad(t, u)` after pushing both triangles (it grows the array; resize it to the
triangle count before `rebuildTopology()` if triangles follow). Read with `quadPartnerAt(t)`, which also
requires both halves to share a face group: what Wire shows as a quad and what exporters write as one.
- Display: `Renderer::uploadFaces` builds GL_LINES edges without the diagonals for meshes with quads.
- Export: `exportFace(t, face)` gives 4 corners (in winding order; an `(a,b,c)+(a,c,d)` split gives `(a,b,c,d)`),
  3, or 0 for the second half of a quad. OBJ, FBX, PLY, USD and GoZ use it; glTF and STL stay triangles.
- Building a mesh from some triangles of another: `remapQuads(src, newIndex, dst)`; appending:
  `appendQuads(dst, firstTriangle, src)`.

## Telling the renderer
- Vertex attributes (mask, colors): `mesh.markDirty(v)` per vertex or `markAllDirty()`.
- Triangle attributes (hidden, face groups): set `mesh.facesDirty = true`.
- `RenderSettings` (src/render/Renderer.h): `showPolypaint` (colors replace the base colour),
  `showMask` (masked areas darken), `showPolygroups` (`polygroupColor(id)`, same hash as the shader).
  Hidden triangles are left out of the index buffer. Toggles live in the View menu.

## Preservation through topology changes
| Operation | mask / colors | face groups / hidden | uvs | quadPartners |
|-----------|---------------|---------------------|-----|--------------|
| `loopSubdivide` (Primitives.cpp) | linear interpolation | children inherit | corner midpoints | each quad -> 4 quads |
| `decimate` / `generateLods` (Decimate.cpp) | exact (vertices are a subset) | surviving faces keep theirs | follow kept vertex (approximate at seams) | dropped |
| `voxelRemesh` (Voxel.cpp) | nearest original vertex (`transferVertexAttributes`) | **reset** (visible, group 0) | dropped | new: every surface-nets quad |
| `weldImported` (Mesh.cpp) | first vertex of each welded group | per kept triangle | per kept triangle | pairs of kept triangles |
| `extractTriangles` / split / delete hidden (Face Groups.cpp) | per kept vertex | per kept triangle | per kept triangle | pairs with both halves kept |
| `mergeMeshes` (Parts.cpp) | concatenated | concatenated | concatenated | concatenated (ids shifted) |
| plane / cube primitives | empty | cube: one group per side | empty | every grid cell |
| icosphere, image relief/inflate | empty | empty | empty | empty |

New topology operations must do the same (`transferVertexAttributes(src, dst)` for rebuilt surfaces).

## Paint layers (src/core/PaintLayer.h)
With paint layers, `colors` is a cache: the composited stack of `paintBase` (the Base layer) and
`paintLayers` (per-vertex colour + alpha, opacity, blend mode, visible, locked); `activePaintLayer` is where
painting goes. Paint through `paintVertex()` (it writes the active layer and recomposites that vertex) and
record undo with `Sculptor::recordColor` / `UndoRecord::captureColors`, which capture the active layer.
Subset operations call `copyPaintLayers(src, dst, kept)`, `loopSubdivide` calls `subdividePaintLayers`;
anything else that changes the vertex count flattens the stack (`sanitizePaintLayers`, run by
`rebuildTopology`), keeping the look in `colors`.

## Undo (src/core/Undo.h)
`UndoRecord` holds sparse `UndoChannel`s: `positions` (layer target), `mask`, `colors`
(per vertex), `hidden`, `polygroups` (per triangle), or a full `mesh` snapshot. Snapshots
(`Document::replaceMeshUndoable`) copy every attribute. Undo allocates a missing attribute
before restoring, so recording values of an empty attribute is fine.
- Strokes: inside `Sculptor::beginStroke/endStroke`, call `sculptor.recordMask(mesh, v)` /
  `recordColor(mesh, v)` **before** writing `mesh.mask[v]` / `mesh.colors[v]`.
- Whole-attribute operations: `doc.undo.push(UndoRecord::captureMask(mesh, "Invert Mask"))`
  (also `captureColors`, `captureHidden`, `capturePolygroups`, optionally for a subset) before changing.

## Brush hooks (src/core/Sculpt.h/.cpp)
- `vertexInfluence(mesh, v)` = 0 for fully hidden vertices, else `1 - mask`.
  `Sculptor::displace` applies it, so **every displacement brush respects mask and hidden**
  (built-in, recipe and Move). Paint brushes should multiply by it; mask brushes must not.
- `brushVertices(mesh, bvh, center, radius, out)` = sphere query minus fully hidden vertices;
  use it instead of `BVH::queryVertices` in new brushes. `Sculptor::mirrors(sym)` gives symmetry.
- Picking: `BVH::raycast` skips hidden triangles, so `App::pick` never lands on them.

## Python and tests
`doc.mask` (N,) float32, `doc.colors` (N,3) float32, `doc.polygroups` (M,) uint32, `doc.hidden` (M,)
uint8: copies, `None` when absent, assignable (`None` clears). New tests: add `tests/test_<feature>.cpp`
including `TestHarness.h` (`TEST`/`CHECK`); CMake globs it. See tests/test_attributes.cpp.
