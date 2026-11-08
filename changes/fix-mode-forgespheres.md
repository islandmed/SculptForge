---
fixed: After **Tool > Forge Spheres > New Forge Sphere**, picking a brush, Draw, Move, Scale or Rotate, or pressing Esc
  goes back to the Part you were sculpting (a Forge Sphere Part isn't sculpted until Make Mesh); before, brushes
  did nothing until you picked that Part again.
testers:
  - Any OS: Tool > Forge Spheres > New Forge Sphere, then pick Clay and drag on the model: it sculpts the Part from before.
  - The same with Esc instead of Clay.
---
Found by the mode matrix (tests/ui/mode_matrix.sfui); its forgesphere.new known lines are deleted.
