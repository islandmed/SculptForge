---
changed: **Trim**: Intensity and pen pressure now scale all the way down: Intensity 1 or a light pen stroke is a light touch. With a mouse, or at full pressure, Intensity 50 and above is unchanged.
testers:
  - Any OS: Trim on a sphere at Intensity 1, 10, 25 and 50; each step cuts visibly deeper, 1 barely shaves the surface.
  - Any OS, pen: Trim at Intensity 50 with a light press is lighter than before; **Soft** on Trim's pressure curve (Brush Lab or Edit > Preferences > Tablet) gives a heavier light press.
  - Any OS: a micro level (Detail) with Trim at Intensity 1 and 50; same difference, and the brush ring preview matches the cut.
---
Trim's gain was 0.5 + Intensity at every setting, so Intensity 1 still cut half of what stood above the plane per dab. It is now
2 x Intensity below 50 and 0.5 + Intensity from 50 up (the default and higher settings at full pressure are bit for bit as before; Intensity is multiplied by pen pressure first, so a light pen stroke at 50 cuts less). Kernel, the micro
grid and tiles, the brush ghost and the viewport preview share the one gain.
