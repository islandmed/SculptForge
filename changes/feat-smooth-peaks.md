---
whats_new: "**Smooth Peaks brush**: a new brush in the Smooth family that only lowers the high points, the parts that stick out above the surrounding surface, and leaves the hollows alone. Shift-smooth is unchanged. Requested by ABHITAR."
highlight: Smooth Peaks brush
added: "**Smooth Peaks** brush: smooths like Smooth, but only brings down the vertices that sit above the average of their neighbours (along their normal); valleys are never filled. Works on the mesh and on micro levels, with masks, symmetry and alphas. Shift-smooth still uses the plain Smooth. Requested by ABHITAR."
testers:
  - Any OS: pick Smooth Peaks (Sculpt > Brush shelf). On a model with bumps and pits, stroke over both; the bumps come down and the pits keep their depth. Then hold Shift with any brush: it still smooths everything.
---
Brush type SmoothPeaks (stored name "SmoothPeaks"); shares Smooth's kernel, offset = min(0, dot(avg - x, n)) along n. Uses the normal brush Intensity slider (Smooth has its own).
