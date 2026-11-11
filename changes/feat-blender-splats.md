---
whats_new: **Splats with Blender**: with the Blender add-on and Blender 5.3, Gaussian splats go to Blender and back with one click, view-dependent colour and placement kept. **File > Export > Gaussian Splats (.ply)** in Blender saves them (Blender can't), and a dropped `.splat` file imports there.
added: **Gaussian splats in the Blender bridge** (Blender 5.3+, which draws and imports splats but cannot export them): **Send to SculptForge** sends a selected splat object as a standard 3DGS PLY with its transform; **File > Blender > Send to Blender** sends the loaded splats back and updates the same object in place. Positions, scales, rotations, opacity and spherical harmonics (up to degree 3) are kept, nothing is baked. The add-on also adds **File > Export > Gaussian Splats (.ply)** and a drop handler for `.splat` files; older Blender versions get a message and the mesh bridge is unchanged. The exchange manifest has an optional `splats` key (older versions skip it). The CI add-on job now also runs bpy 5.3.0.
testers:
  - Blender 5.3 + the add-on: import a splat PLY (drop it on the viewport), select it, Send to SculptForge; it loads as a reference with its colour. In SculptForge choose Flip, then File > Blender > Send to Blender; the same Blender object updates and turns over.
  - Blender 5.3: File > Export > Gaussian Splats (.ply), open the file in SculptForge or a splat viewer; it looks like the object.
  - Blender 4.2 to 5.2: Send with nothing selected still works for meshes; File > Import > Gaussian Splats (.splat) says it needs 5.3.
---
Blender-side attribute names verified against the bpy 5.3.0 wheel and the public PR 163102; design in docs/blender.md.
