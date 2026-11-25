---
changed: **Trim**: Intensity below 50 now cuts proportionally less: 1 is a light touch (50 and above unchanged).
testers:
  - Any OS: Trim on a sphere at Intensity 1, 10, 25 and 50; each step cuts visibly deeper, 1 barely shaves the surface.
  - Any OS, pen: Trim with light pressure at Intensity 50 behaves like a low Intensity with full pressure.
  - Any OS: a micro level (Detail) with Trim at Intensity 1 and 50; same difference, and the brush ring preview matches the cut.
---
Trim's gain was 0.5 + Intensity at every setting, so Intensity 1 still cut half of what stood above the plane per dab. It is now
2 x Intensity below 50 and 0.5 + Intensity from 50 up (the default and higher settings are bit for bit as before). Kernel, the micro
grid and tiles, the brush ghost and the viewport preview share the one gain.
