---
whats_new: **Save Visible Parts As** keeps your material look: the copy opens with the same shading (PBR, environment, exposure, Lit Sphere), view and settings as a normal Save As.
fixed: **File > Save Visible Parts As...** no longer drops the scene's view and material settings (PBR or Lit Sphere shading, environment, exposure, the Lit Sphere in use, colour tinting, camera and saved views); the copy opened with the ones from the last full save, so shiny and metal materials looked plain.
testers:
  - Any OS: paint a metal material on one Part (Color > Material), set Material > Viewport Shading > PBR, hide another Part, File > Save Visible Parts As..., open the copy; the metal and the PBR look are there.
---
The Parts' texture, material channels and vertex colour were always written; the view settings were only stored by Save As.
