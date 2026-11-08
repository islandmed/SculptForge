---
whats_new: **Keep original** for **Tool > Parts > Split**: tick it and Hidden, Masked or Unmasked copy those triangles into a new Part, leaving the source Part as it is.
added: **Keep original** checkbox under **Tool > Parts > Split** (Hidden / Masked / Unmasked; `keep=true` on
  `part_split_hidden`, `part_split_masked`, `part_split_unmasked`): the triangles are copied into the new Part (UVs,
  face groups, vertex paint, sculpt layers; the copy is cut at the current subdivision level) and the source Part
  stays untouched, levels and mask included. One undo step removes the copy. Requested by _tfx on Discord.
testers:
  - Any OS: mask half a sphere, tick Tool > Parts > Keep original, press Split > Masked: a new "<Part> masked" Part
    appears below with the masked half, the sphere is still whole and still masked; Ctrl+Z removes only the new Part.
  - With Keep original off, Split > Masked still moves the triangles as before.
---
