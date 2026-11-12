---
changed: **Tool > Geometry > Micro Detail**: before Add Micro, the slider reads **New Micro 2 (83M)**: what Add Micro would make,
  not a level the mesh already has.
added: Windows laptops with two graphics chips (Intel and NVIDIA or AMD) now start SculptForge on the faster one without
  a setting; **Preferences > Performance > Graphics card** and Windows' own graphics settings still decide if you pick one.
testers:
  - Windows laptop with Intel graphics plus an NVIDIA or AMD card (GTX 1650 class), on a fresh install with no Windows
    graphics setting for SculptForge: start it from a terminal or open the log; the `OpenGL ... on ...` line names the NVIDIA
    or AMD card, not the Intel one. Then set **Preferences > Performance > Graphics card** to the Intel one and restart: the line
    names Intel.
---
Kept from fix/stroke-lod-flicker (910d0951, 60ace8c0) when its LOD change was pulled from 0.12.13 for a 120 px stroke cost.
