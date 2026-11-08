---
whats_new: **The Tool palette in the order you work**: Parts, then shaping (Geometry, Remesh, **Reshape**, Cut,
  Layers), masking and groups, painting (Paint Layers, **Texture Image**), then Bake and Export, with **Viewport Look**
  last. **Reshape** lists Smooth, Relax, Polish and Balloon from gentlest to strongest, and **Mirror** now sits beside
  **Make Symmetric** in **Tool > Geometry > Symmetry**. Extract's button is **Make Shell**, the mesh check is
  **Find Problems**, and **Edit > Preferences > Saving & Layouts** holds Save Layout, Reset and Export / Import Interface.
  Shortcuts, shelves, scripts, saved layouts and files are unchanged, and the command search (F3) still finds each
  one by its old name.
changed: Tool sub-palettes renamed and reordered by task: Reshape, Texture Image and Viewport Look
  (were the generic deform, texture and display names); Reshape ordered Smooth, Relax, Polish, Balloon; Mirror moved to Tool > Geometry >
  Symmetry; Make Shell (Extract's button), Find Problems (Tool > Geometry > Cleanup, also in F3), Preferences > Saving
  & Layouts. Sub-palette and action ids are unchanged (tool.deformation, tool.texture, tool.display,
  preferences.config, deformation.mirror, part.extract), so saved layouts, hotkeys, shelves and scripts keep working;
  translations updated (Spanish, Traditional and Simplified Chinese).
testers:
  - Any OS, 1440 px and 1024 px wide, UI scale 100% and 200%: the Tool palette's list reads Parts ... Viewport Look;
    open Reshape, Tool > Geometry (Symmetry: Make Symmetric and Mirror), Parts (Make Shell), Texture Image and
    Viewport Look; no label is clipped.
  - A layout saved before this version with the deform or display sub-palette open: it opens Reshape or Viewport Look.
  - F3, type the old names (for example "check mesh", "deformation", "config"): Find Problems, the Reshape operations and the Saving & Layouts
    actions are listed.
---
Reviewers: follow-ups 1-5 of ~/Work/.sf/ip-labels-audit.md (competitor palette phrases at the sub-palette level),
on top of fix/ip-labels. The old phrases are refused by tools/check_marks.py (labels / whole-labels blocks in
docs/legal/trademarks.md). Extract's other labels stay with fix/extract-labels (only part.extract's label changes
here; whoever merges second rebases Config.cpp, PanelExtract.cpp, tests/ui/extract_mirror.sfui).
