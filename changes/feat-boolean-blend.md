---
whats_new: **Boolean blend**: the **Blend** slider under **Tool > Parts > Boolean** rounds the seam where a Part meets the
  Parts above it with a soft fillet of that radius; Add fills the crease, Subtract and Intersect round the rim. Live in the
  preview, applied by Make Boolean Mesh, saved with the Part.
highlight: Soft blends on Booleans
added: **Boolean blend** (Tool > Parts, the **Blend** slider under the Boolean row): each Part below the base gets a soft
  fillet of that radius (scene units, 0 = sharp) where it meets what the Parts above it made, the way distance-field
  modellers round a merge or a cut. An Add fills the crease, a Subtract or Intersect rounds the rim. The preview follows
  the slider; **Make Boolean Mesh** refines the faces along the seam and moves them onto the rounded surface, and leaves
  everything farther than the radius from the seam bit for bit as it was (face groups, vertex paint and UVs included); the
  result stays watertight. The voxel fallback blends the same way. Saved in projects with the Part's Boolean; a duplicate
  keeps it. `part_boolean` and `boolean_parts` take `blend` (scripts, Python, MCP). Requested on Discord.
testers:
  - Any OS: Tool > Parts, Append a Sphere, move it half out of the cube, set both to Add; select the sphere and drag Blend:
    the crease becomes a smooth fillet in the view as you drag, and 0 is sharp again.
  - Set the sphere to Subtract with a blend: the rim of the hole is rounded. Make Boolean Mesh, then Tool > Geometry: no
    open edges; the face groups of the cube's sides and the sphere are still there.
  - Save the project, reopen it: the Blend value is still on the Part; Duplicate the Part: the copy has it too.
---
Core: src/core/BooleanBlend.{h,cpp} (band refinement + smooth-min projection), hooks in MeshBoolean.cpp (exact + voxel),
Part::booleanBlend, PartBoolean operands and commands, the Parts panel slider. Tests: tests/test_boolean_blend.cpp,
tests/ui/boolean_blend.sfui (+ 1440/2880).
