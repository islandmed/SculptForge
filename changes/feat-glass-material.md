---
whats_new: **Glass, water and diamond**: **Render > Materials > This Part** makes a Part see-through, with an index of refraction (water 1.33, glass 1.5, diamond 2.42), edges that reflect the environment more, and a tint that deepens where the glass is thick. Saved in the project and in glTF.
highlight: Glass, water and diamond Parts
added: **Glass, water and diamond** (Render > Materials > This Part, `part_glass`): a Part can be see-through. **Transmission**, **Index** (presets Water 1.33, Glass 1.5, Diamond 2.42), **Tint** and **Depth** are saved per Part in the project (older files read as ordinary Parts) and are one undo step. The view draws glass Parts after the opaque ones, farthest first, each its back faces and then its front faces, with a Fresnel opacity from the index and a thickness tint from a back-face depth pass; it does not bend the scene behind the glass, and dispersion and caustics are left to the Render Bridge. glTF export and import carry `KHR_materials_transmission`, `KHR_materials_ior` and `KHR_materials_volume` (listed as used, never required). Scenes with no glass Part draw exactly as before. A graphics chip that can't make the back-face depth target draws the glass clear, and the panel says so. A Part with Adaptive Detail levels is drawn opaque.
member_request: true
testers:
  - Windows, Linux and the Android tablet: Render > Materials > This Part > Glass on a sphere placed in front of a painted Part; the painted Part shows through, the sphere's edges mirror the environment (PBR view) or the lit sphere (Lit Sphere view).
  - Pick Water, then drag Depth up: the thick middle of a ball tints deeper than its thin rim. Tint turns it to coloured glass. Undo (Ctrl+Z) takes each change back in one step.
  - Export a glass Part to .glb and open it in a viewer that supports transmission (Blender): it reads as glass with the same index and tint; open the .glb in SculptForge again and it is glass.
---
Slice 1 of the glass material (feature queue). Next: refraction of the scene behind (screen space), frosted blur, Material Recipes v2 with Clear Glass, Water, Diamond and Frosted Glass, USD notes.
