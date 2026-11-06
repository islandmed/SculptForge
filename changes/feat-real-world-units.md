---
whats_new: **Real-world size**: parts for 3D printing keep their size through every export. Pick the unit sizes show in
  (mm, cm, m, in) in **Edit > Preferences > Interface > Units**; **Tool** shows the Part's size and **File** the size exports
  write. OBJ and PLY files can be read in mm, cm, m or in (**OBJ/PLY in**). Asked for by @Swar55.
highlight: Parts keep their real-world size, in mm, cm, m or in
added: **Units** (Edit > Preferences > Interface, #148, thanks @Swar55): sizes show in mm, cm, m or inches: the brush size,
  the active Part's width x height x depth under its name in Tool, and the size exports write in File > Import /
  Export. **OBJ/PLY in** reads the numbers in OBJ and PLY files (which have no units) as mm, cm, m (the default) or
  inches; `open`, `open_obj` and `part_append` take `unit=` for scripts, and STL files may be read as inches too.
fixed: Exports keep the size a part was opened with (#148, @Swar55): a scene opened from an STL, OBJ or PLY writes
  OBJ and PLY in that file's unit (an STL opened in millimetres used to come out of OBJ export 1000x smaller), an
  STL opened in another unit is written back in it, and decimal sizes round-trip exactly (12.7 mm stays 12.7, not
  12.700001). Projects remember the unit.
fixed: USD files in centimetres (`metersPerUnit = 0.01`, as Maya and Houdini write) open at their real size instead
  of 100x too large, to the exact millimetre (#148, thanks @NicTanghe).
testers:
  - Open an STL from your slicer (e.g. a 20 mm calibration cube): Tool shows 20 x 20 x 20 mm. File > Export OBJ and
    Export STL, open both in the slicer: both are 20 mm.
  - Edit > Preferences > Interface > Units > in: Tool and the brush size read in inches; restart, it is kept.
  - Edit > Preferences > Interface > OBJ/PLY in > mm, open an OBJ made for printing (numbers in mm): Tool shows its mm size;
    Export STL; the slicer shows the same size.
  - A .usda from Maya (centimetres): opens at its real size (a 180 cm figure is 1.8 m, not 180 m).
---
Step 3 of the brief (two-point Measure) is not in this branch: follow-up.
