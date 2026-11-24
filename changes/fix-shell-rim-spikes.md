---
fixed: "**Extract > Make Shell**: with Edge Smooth above 0 the outline of a shell made from masked areas follows a smooth line again. Before, the rim came out raised, ridged and jagged (spiky even with Edge Smooth turned up), most of all around thin necks and tips of the mask."
testers:
  - "Any OS: on a dense part (a few hundred thousand triangles or more) paint a mask in irregular islands, narrow strips and small spots, then Tool > Parts > Extract > Make Shell with the defaults (Edge Smooth 5, Thickness 0.015): the shell's outline is a smooth line with no raised or ridged rim and no black speckle along it. Try Edge Smooth 0, 10 and 50, and Both Sides, Corners and Border."
  - "Any OS: Edge Smooth 0 still follows the mask's triangles exactly as masked."
---
Edge Smooth moved the edge vertices on their own, so the triangles beside them (a tip of one triangle, a one-triangle neck) folded over; the shell's offset then ran along flipped one-sided normals. Now no vertex closes more than a quarter of a triangle's height per step or moves farther than a few edges, the vertex rows inside the edge follow, anything still turned over goes back toward where it was, and the rim's offset follows the source surface's normals. Test extract_shell_rim_follows_a_smooth_outline_without_spikes.
