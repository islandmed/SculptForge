---
fixed: Clay, Clay Block, Flatten and Trim on micro levels have a safeguard against stretched triangles on big moves:
  points facing away from the brush plane stay put, and no point moves more than a quarter of the brush per dab.
  Ordinary strokes look and feel the same.
---
Re-applies the grid half of f084f9fd only; the base fold fallback (the floor slowdown) stays reverted. Target 0.12.1.
