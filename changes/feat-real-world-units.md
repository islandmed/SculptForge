---
whats_new: **Real-world size**: parts for 3D printing keep their size through every export. Pick the unit sizes show in
  (mm, cm, m, in) in **Edit > Preferences > Interface > Units**; **Tool** shows the Part's size and **File** the size exports
  write. OBJ and PLY files can be read in mm, cm, m or in (**OBJ/PLY in**). Asked for by @Swar55.
highlight: Parts keep their real-world size, in mm, cm, m or in
added: **Units** (Edit > Preferences > Interface, #148, thanks @Swar55): sizes show in mm, cm, m or inches: the brush size,
  the active Part's width x height x depth under its name in Tool, and the size exports write in File > Import /
  Export. **OBJ/PLY in** reads the numbers in OBJ and PLY files (which have no units) as mm, cm, m (the default) or
  inches; `open`, `open_obj` and `part_append` take `unit=` for scripts, and STL files may be read as inches too.
fixed: **OBJ and PLY exported from an STL are now the right size: 1000x bigger than in 0.11.9.** 0.11.9 read the
  STL in millimetres but wrote OBJ and PLY in metres, so a 50 mm part came out as 0.05 (#148, thanks @Swar55). A scene
  opened from an STL, OBJ or PLY now writes OBJ and PLY in that file's own unit, an STL opened in another unit is
  written back in it, and decimal sizes come back exactly (12.7 mm stays 12.7, not 12.700001). Projects remember the
  unit. If you scaled these exports by 1000 in another app, you no longer need to.
fixed: **USD stages in centimetres now open at their true size: 100x smaller than in 0.11.9.** 0.11.9 ignored
  `metersPerUnit`, so a stage from Maya or Houdini (`metersPerUnit = 0.01`) opened 100x too large: a 180 cm figure
  was 180 m. It is now 1.8 m (#148, thanks @NicTanghe). Stages in metres, and USD files SculptForge wrote, open as
  before.
testers:
  - Open an STL from your slicer (e.g. a 20 mm calibration cube): Tool shows 20 x 20 x 20 mm. File > Export OBJ and
    Export STL, open both in the slicer: both are 20 mm.
  - Edit > Preferences > Interface > Units > in: Tool and the brush size read in inches; restart, it is kept.
  - Edit > Preferences > Interface > OBJ/PLY in > mm, open an OBJ made for printing (numbers in mm): Tool shows its mm size;
    Export STL; the slicer shows the same size.
  - A .usda from Maya (centimetres): opens at its real size (a 180 cm figure is 1.8 m, not 180 m).
---
Step 3 of the brief (two-point Measure) is not in this branch: follow-up.
