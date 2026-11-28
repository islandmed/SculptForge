---
whats_new: **Quad Remesh** (Geometry): **50%** and **200%** halve and double the Target Quads, click after click, so 100,000 steps down to 50,000, 25,000, 12,500.
added: **Quad Remesh Target steps**: the 50% and 200% buttons act on the Target Quads value itself (halve, double), so repeated
  clicks keep going; the first click, before the Target has been set, starts from the Part's polygon count as before. The
  target stops at 100 and 2,000,000 with a note in the status line. 100% still sets it to the Part's polygon count.
member_request: true
testers:
  - Any OS: Geometry > Quad Remesh, type 100000 into Target Quads, click 50% three times; it reads 50000, 25000, 12500. Click 200% twice; 25000, 50000.
---
