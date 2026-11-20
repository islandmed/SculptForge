---
whats_new: **Creature Skin brushes**: sixteen high-resolution brushes for hide wrinkles, snake and lizard scales, croc
  plates, skin and nose pores and fine skin, tuned to read at micro level 3 or 4 (**Sculpt > Brush**, pack Creature
  Skin). Requested by @RodTDai.
whats_new: **Alphas from 16-bit PNG and OpenEXR** keep their full precision: smooth height maps no longer step.
highlight: Creature Skin: 16 high-resolution skin, hide and scale brushes
added: **Creature Skin** brush pack and alphas (gh#228): Elephant Hide, Hide Folds, Dinosaur Hide, Snake Scales,
  Overlap Scales, Pebble Scales, Scute Plates, Pores Fine, Pores Clustered, Pores Stretched, Nose Pores Open, Nose
  Pores Dense, Nose Pores Furrowed, Skin Crosshatch, Skin Fine Lines and Skin Micro Grain. Each alpha is 2048 x 2048,
  made in code by SculptForge the first time a brush uses it (no image files), and repeats without a seam (Alpha >
  Tile). The brushes are tuned for micro levels: scales and plates step whole rows per dab, pores and grain turn each
  dab. Requested by @RodTDai.
added: Alpha images: **OpenEXR** (half or float, any compression) and **16-bit PNG** load at full precision, with no
  8-bit step, up to 2048 pixels a side (was 512); an EXR outside 0 to 1 is stretched to its own range. The Alphas
  folder and alpha pools take .exr too.
testers:
  - Any OS: open a head, Tool > Geometry: Micro 3, Add Micro; pick Sculpt > Brush > Pores Fine, stroke a cheek, zoom in:
    round pores, no dark specks or spikes, nothing raised.
  - Snake Skin: draw a straight stroke down a neck: rows of overlapping diamond scales follow the stroke, row on row.
  - Load a 16-bit PNG or an EXR height map as an alpha (Sculpt > Alpha > Load image...): a smooth ramp stamps without
    terraces.
  - Windows: first pick of a Creature Skin brush: no long pause (well under half a second).
---
Close-ups on the lion head at micro level 3 (tests/artist/closeup_creature_skin.sfui): before = the classic brush,
after = the Creature Skin one, same stroke.
