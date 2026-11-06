---
whats_new: **Dynamic on a subdivided model**: turning on **Dynamic** on a Part with subdivision levels now offers
  **Freeze Levels and Turn On Dynamic**: the level you see becomes one plain mesh (Ctrl+Z brings the levels back).
whats_new: **Saved views**: **View > Views** has Front, Back, Left, Right, Top and Bottom (Numpad 1, 3, 7; Ctrl for
  the other side) and **Save View** for your own named camera angles; Numpad 0 goes back to the last one. They are
  saved with the project.
whats_new: **A readable mask**: masked areas show a soft, dark tint instead of near-black, with **Mask darkness** in
  Preferences > Interface and Tool > Masking. Dense wireframes no longer turn into a dark moire.
whats_new: **Easier to see**: options that are on stand out in every colour scheme, and Quad Remesh guide curves draw
  smooth with an outline.
whats_new: **Safer levels**: in Tool > Geometry, **Divide (keeps levels)** and **Subdivide (removes levels)** no longer
  sit together, Micro has its own group, and Subdivide, Delete Lower and Delete Higher ask before removing levels
  (with **Don't ask again** for Subdivide).
highlight: Saved camera views and a readable mask
added: Freeze Levels (`subdiv_freeze`, `dyntopo freeze_levels=true`): turning on Dynamic on a Part with subdivision
  levels offers to keep the level shown as one plain mesh and turn Dynamic on, one undo step, instead of a dead-end
  message. The Draw palette shows the same button while Dynamic waits on levels.
added: View > Views: Front, Back, Left, Right, Top and Bottom buttons (Numpad 1 / 3 / 7, Ctrl for the opposite side,
  in every input style), and named saved views: Save View (Ctrl+Alt+Numpad 0), click a name to go back, Numpad 0 for
  the last one. Saved with the project.
added: Mask darkness (Preferences > Interface and Tool > Masking).
changed: Masked areas show as a cool dark tint with a soft edge instead of near-black, so the form stays readable.
changed: The wireframe fades edges shorter than a few pixels: a dense mesh shows a light veil, not a dark moire.
changed: Options that are on (toggles) have an accent-tinted fill in every colour scheme, not just a lighter grey.
changed: Quad Remesh guide curves draw smooth, with stroke smoothing while drawing and a dark outline for contrast.
changed: Tool > Geometry (from Discord): **Divide (keeps levels)** and **Subdivide (removes levels)** are named for
  what they do and sit apart: Micro in its own Micro Detail group, Subdivide under Change the Mesh (no levels). On a
  Part with levels, Subdivide asks first ("This removes your N subdivision levels"), with Don't ask again; Delete
  Lower and Delete Higher ask too (Ctrl+Z still brings the levels back).
changed: Tool > Parts > Extract says to mask first when there's no mask, instead of only greying out its buttons.
testers:
  - Default sphere, Ctrl+D twice, then click **Dynamic**: you should get **Freeze Levels and Turn On Dynamic**. Sculpt
    small Clay / Standard strokes (Size about 19, X symmetry); Ctrl+Z brings the levels back.
  - Mask part of the model with Wireframe on: the mask reads at the default **Mask darkness** and the wire still shows.
    Try the slider in Tool > Masking.
  - **View > Views**: the six axis buttons and Numpad 1/3/7 (Ctrl for the opposite side) in your input style; save two
    named views, save the project, reopen, restore them (Numpad 0 = the last one).
  - Quad Remesh guides: curves draw smooth and stand out; "on" toggles are easy to tell from "off" in your scheme.
  - Tool > Geometry: Subdivide (removes levels) asks first on a Part with levels (try Don't ask again, then Ctrl+Z);
    Delete Lower / Delete Higher ask too.
---
Rebuilt on 0.11.2 main from fix/review-voidart 3302e4a2 (video review of 0.10.6 + Discord, promised 11-03).
