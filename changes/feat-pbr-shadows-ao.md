---
whats_new: **Shadows and ambient occlusion in the view**: the key light casts shadows on the sculpt (the nose on the cheek,
  the brow over the eyes) and creases and hollows darken softly. **Material > Viewport Shading** (PBR) and **Preferences >
  Display > Shadows & AO**: on, off or Auto, with AO strength, AO softness and shadow softness.
highlight: Shadows and ambient occlusion in the PBR view
added: **Shadows & AO** (Material > Viewport Shading in the PBR view, Preferences > Display): cast shadows from the key
  light (in PBR, the environment's brightest direction) and screen-space ambient occlusion, worked out from the drawn
  view, blurred with a depth-aware filter. Shadows and Ambient occlusion are each On, Off or Auto (the default: on from the
  Standard hardware tier up, off on the Essential tier and on phones and tablets); AO strength, AO softness and Shadow
  softness sliders; "With Lit Sphere shading" adds them to the Lit Sphere and studio shading. While you orbit or sculpt they
  step aside if the frame would fall behind, and come back when the view settles. Saved with the preferences;
  scriptable as `view_shadows`. Both off draws the view exactly as before.
testers:
  - Any OS, Standard tier or up: Material > Viewport Shading > PBR on a sculpted head; the nose shadows the cheek, the
    brow shadows the eyes, and the ears and nostrils read deeper. Toggle Shadows and Ambient occlusion Off/On to compare.
  - Drag AO softness and Shadow softness: the darkening widens and the shadow edges soften, with no halo around the model.
  - Essential tier: Auto leaves both off; set them On and orbit a big mesh: they drop out while
    moving if the frame can't keep up and return when you stop.
  - Preferences > Display > With Lit Sphere shading: the same in the default sculpt view.
---
Screen-space by design: the key light turns with the view, so a shadow map would draw the scene a second time every
orbit frame; this pass costs the view's pixels (half resolution), never the triangles. Occluders off screen cast nothing.
