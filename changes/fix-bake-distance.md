---
whats_new: **Bake search distance**: a bake whose search distance was too short now says how much of the map found no surface and which distance fixes it, with a **Bake Again with Suggested Distance** button.
changed: **Bake** (Tool > Bake, `bake`): when more than 0.5% of the texels found no surface because the search distance was too short, the panel, the status line and the command's output say how much of the map missed and which Front / Rear reach the high-poly for those texels, and **Bake Again with Suggested Distance** bakes again with them. The panel shows the distance Auto measured, and the guide's Baking chapter names the trap.
testers:
  - Tool > Bake with a low-poly and its high-poly; turn Auto distance off and set Front and Rear to 0.1%; Bake. A red line gives the share of the map that found no surface and a distance; click Bake Again with Suggested Distance, the line is gone.
  - Same with Auto distance on: Front and Rear under the Bake button show what it measured and no warning appears.
---
Reviewers: src/core/Bake.cpp (miss sample + long rays after the pass), PanelBake.cpp; test: ctest -R sculptforge_tests (bake_short_distance_warns_and_suggests, bake_auto_distance_leaves_no_holes), ui_bake.
