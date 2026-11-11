---
whats_new: **More warnings before levels go**: Delete Masked, Remove Hidden, Split, Mirror, Make Symmetric and Merge by Distance ask before they drop a Part's subdivision levels.
highlight: Warnings before destructive operations
added: **Warnings for more destructive operations** (#173, thanks @TheLycanFenrir): Delete Masked, Remove Hidden, Split, Mirror,
  Make Symmetric and Merge by Distance rebuild the mesh and drop the subdivision levels, so on a Part with levels they now
  ask first, like Voxel Remesh, Quad Remesh and Decimate do (Freeze Levels and Proceed / Cancel). Ctrl+Z still brings the
  levels back, a Part without levels is not asked, and "Keep original" splits don't ask. **Preferences > Interface > Ask before
  dropping subdivision levels** turns the asking off or back on for all of them (and Subdivide).
testers:
  - Any OS: sculpt a Part, Geometry > Subdivide a few times, mask part of it, Delete masked (Face groups); the pop-up names the levels. Cancel keeps everything, Proceed deletes and Ctrl+Z restores the levels.
  - Preferences > Interface: untick "Ask before dropping subdivision levels"; the same operations now run without the pop-up.
---
Extends the #173 pop-up (shipped 0.12.1) to the other operations that drop levels through the interface; scripts and macros
run directly as before. Remove Below / Above and Subdivide already had their own confirms.
