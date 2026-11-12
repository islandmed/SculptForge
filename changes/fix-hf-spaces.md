---
fixed: **Generate 3D** (free Hugging Face models): Hunyuan3D 2, 2 mini Turbo and 2.1 and TRELLIS.2 now find their image and prompt inputs
  by name, so the Spaces adding or reordering inputs (Hunyuan3D now takes 13, TRELLIS.2 15) no longer breaks them. Everything
  else uses each Space's own defaults.
testers:
  - Any OS, Preferences > AI > Hugging Face: Generate 3D on a photo with Hunyuan3D 2.1, 2 and 2 mini Turbo; each imports a mesh.
  - TRELLIS.2 with a free Hugging Face token: same photo; a mesh comes back, or the panel says the free GPU time is used up.
---
Presets named: hunyuan3d-2.1, hunyuan3d-2, hunyuan3d-2-turbo, trellis-2 (text presets hunyuan3d-2, hunyuan3d-2-turbo). Same hosts and calls as before.
