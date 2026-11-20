---
added: Alpha images from OpenEXR: a height map below zero with transparent pixels keeps them at no effect (the range
  is taken over the pixels that show); a file larger than 16384 pixels a side is refused from its header, before its
  pixels are read; a downscaled image keeps its last column and row.
---
Follow-up to feat/creature-skin-pack (same unreleased version): extends its "added:" alpha-file entry.
