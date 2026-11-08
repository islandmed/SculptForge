---
whats_new: **Tool > Geometry, grouped by what you do**: **Levels** (the **Level** slider, **Lower / Higher Level**,
  **Add Level** with **Smooth**, **Rebuild Lower Levels**, then **Remove Below / Remove Above**), **Micro Detail**,
  **Resolution** (Decimate, Quad Remesh, Subdivide), **Cleanup** (**Fill Holes**, Merge by Distance, Check Mesh)
  and **Symmetry** (**Make Symmetric**). Quad Remesh controls say what they do too (**Target Quads**, **50% / 100% /
  200%**, **Size Variation**, **Follow Face Groups**, **Keep Hard Edges**, **Guide Strength**), and **Remove Hidden**
  in Tool > Visibility. Shortcuts, shelves, scripts and saved files are unchanged, and the
  command search (F3) still finds each control by the words it had before.
changed: Tool > Geometry is reorganised by task (Levels, Micro Detail, Resolution, Cleanup, Symmetry) with clearer
  names; your hotkeys, scripts and search keep working.
testers:
  - Any OS, 1440 px and 1024 px wide, UI scale 100% and 200%: Tool > Geometry; the five groups show, no label is
    clipped, Add Level + Smooth share one row.
  - Load a Sphere, Ctrl+D twice, drag the Level slider to 1, Remove Above asks first; Ctrl+Z brings the levels back.
  - F3, type an old word for a control (for example the old name of Fill Holes): the renamed control is listed.
---
Reviewers: competitor palette phrases found by merge-0106 during the 0.11.15 batch review (thanks). Audit with every
match and the follow-up proposals (palette-level names): ~/Work/.sf/ip-labels-audit.md. The phrases are refused by
tools/check_marks.py (labels / whole-labels blocks in docs/legal/trademarks.md, the one file that spells them).
Extract's labels are renamed on fix/extract-labels (this branch is rebased on it).
