---
whats_new: **Micro levels in the PBR view**: a mesh with a micro level (Geometry > Micro) now draws in **Material > Viewport
  Shading > PBR**; before, it vanished and only the other Parts showed.
fixed: A mesh with a micro level (Geometry > Micro) drew nothing in the PBR view (Material > Viewport Shading > PBR): the
  environment and its lookup table shared texture units with the micro level's own data, so the graphics driver refused
  every draw. Lit Sphere and the other shadings were not affected.
testers:
  - Any OS: Geometry > Add Micro on a sculpt (or open a project with a micro level), then Material > Viewport Shading >
    PBR: the model stays drawn and lit by the environment, also while orbiting and with Flat on.
---
Root cause: meshProgram bound uEnvTex/uBrdfLut to units 10/11, which MicroRenderer uses for uDisp/uLevels (buffer
textures, 8-15 with Focus Detail's pages/pool). Micro programs now take units 3/4 (free there: no UVs or painted texture).
Not a size limit: any micro level failed. tests/ui/micro_pbr.sfui fails on v0.12.15 (centre luma 10) and passes here.
