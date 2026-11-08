---
whats_new: **Paint Intensity**: in Paint mode the **Intensity** slider (top bar, Draw palette, Quick Menu, U) sets how
  strongly the paint colours, for every stroke type, with or without an alpha.
fixed: Paint mode ignored the Intensity slider: Intensity 1 and 100 painted the same, with every stroke type, with or
  without an alpha. The slider set the sculpt strength, which Paint doesn't use. In Paint mode Intensity is now the
  Colour intensity (the same value as I); sculpting keeps its own Intensity, and RGB while sculpting still paints at
  the Colour intensity (#164). Thanks @fxrtst
testers:
  - Any OS: Paint (P), Drag Stamp with the Cobblestone alpha, Intensity 1 then 100: 1 is a faint tint, 100 full colour.
  - Same with Dots, Spray and Lazy Stroke, and with no alpha.
  - Back to Sculpt: the top bar's Intensity is the sculpt value again, unchanged.
---
Root: the only Intensity slider on the top bar edited `strength_`, while `App::paintAt` (the one place every paint
dab goes through) uses `rgbIntensity_`. `App::intensityValue()` now picks the value for all Intensity sliders.
