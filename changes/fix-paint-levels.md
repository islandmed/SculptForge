---
whats_new: **Paint across subdivision levels**: a paint stroke on a lower level blends into the finer detail above it at the stroke's strength. Fine dots and edges the stroke did not touch keep their exact colour (no more black dots or pale halos).
fixed: **Vertex paint across subdivision levels**: a low-opacity stroke at a low level darkened the fine detail painted on a higher level to black and left a pale halo around it, because the low level's colour difference was added on top of detail that was not that colour. The stroke now goes up as the same blend (old colour toward the stroke colour at the stroke's amount), so detail it does not reach is unchanged and no channel ends outside the old and stroke colours.
testers:
  - Any OS: subdivide a sphere to 4+ levels; on the highest level paint small dark-red dots and blobs with the Paint brush. Go to level 1, paint a green stroke at low Intensity over them, go back to the highest level: the dots stay red (a little greener under the stroke), no black, no pale ring.
  - Same with Alt (erase) at level 1: the dots fade toward white, never black.
---
Replaces the difference rule of fix/vpaint-subdiv (0.12.6/0.12.12). Paint layers still use the difference rule (follow-up).
