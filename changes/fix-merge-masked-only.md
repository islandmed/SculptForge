---
whats_new: **Masked only** in **Tool > Geometry > Merge by Distance**: weld just the masked points, for example one seam,
  and leave the rest of the Part alone.
added: **Masked only** (Tool > Geometry > Merge by Distance): only masked points merge, with each other; the count
  and the orange preview dots follow it. `weld_points masked_only=true` for scripts. Thanks foirgairwolf on Discord.
testers:
  - Any OS: mask one seam of an unwelded model, tick Masked only in Merge by Distance: the count and the dots cover only
    the masked points, and Merge welds only them (one Ctrl+Z undoes it).
---
Discord #ideas msg 1568861977042755767.
