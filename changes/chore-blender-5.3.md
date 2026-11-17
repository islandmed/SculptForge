---
whats_new: **Blender 5.3 supported**: the Blender bridge is tested on Blender 4.2 LTS, 5.2 and 5.3.
added: Blender 5.3 supported by the Blender bridge (tested on Blender 4.2 LTS, 5.2 and 5.3 in CI).
testers:
  - Blender 5.3: install the add-on, send a mesh to SculptForge and back; it updates in place.
---
Issue #218. The CI matrix already runs bpy 4.2.0, 5.2.3 and 5.3.0 (5.3.0 came with the splat round trip); this bumps the upstream watch target to 5.3 and names the tested versions in the docs.
