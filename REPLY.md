# Reply drafts (no signature)

**Discord #wip-and-feedback, Divide vs Subdivide / Micro (11-02):**
Thanks, you're right that the destructive one sat too close. In 0.10.9 Tool > Geometry has **Divide (keeps levels)** with the level buttons, **Micro Detail** in its own group, and **Subdivide (removes levels)** further down under "Change the Mesh (no levels)". On a Part that has subdivision levels, Subdivide now asks first ("This removes your N subdivision levels. Undo brings them back.") with a Don't ask again box, and Del Lower / Del Higher ask too.

**0.10.6 video review (dynamic topology, mask, views):**
Thanks for the thorough test. 0.10.9 fixes what you ran into: clicking Dynamic on a divided sphere now offers **Freeze Levels and Turn On Dynamic** (one click, Ctrl+Z undoes it) instead of only a message; masks show a readable dark tint with a soft edge (Mask darkness is a preference) and dense wireframes no longer go black over them; **View > Views** has Front/Back/Left/Right/Top/Bottom (Numpad 1/3/7, Ctrl for the other side) plus named saved views (Numpad 0 goes back); Extract says to mask first when there's no mask; on/off toggles and the Quad Remesh guide curves are much easier to see.
