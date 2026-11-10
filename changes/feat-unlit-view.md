---
whats_new: **Unlit view** (Render menu, <kbd>Alt</kbd>+<kbd>L</kbd>): shows the base colour alone, with no lighting, so painted colour reads exactly as stored.
added: **Unlit view** (#168, thanks @nagdav853): <kbd>Alt</kbd>+<kbd>L</kbd>, Render > Materials > Unlit and the right shelf's View list (`set_unlit`; the right shelf has no room for another default button, but Customize can add one) show vertex paint, texture paint and the PBR base colour with no lighting, Lit Sphere or PBR shading. The mask tint and wireframe still draw; the status bar shows Unlit while it is on. Saved with the project.
testers:
  - Paint a few colours on a sphere, press Alt+L: the colours show flat, as picked, with no shading; Alt+L again returns the shaded view.
  - Switch to PBR (Material > Viewport Shading) with a textured Part: Unlit shows the base colour map alone.
  - Unlit with Wire and a mask on: both still show. Save, reopen: Unlit is still on.
---
Persisted per project (not a preference): a new session starts shaded. Key Alt+L is free in Classic, Blender and Maya styles.
