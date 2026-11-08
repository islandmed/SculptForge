---
whats_new: **Tidier Reshape panel** (thanks foirgairwolf on Discord): **Tool > Reshape** reads top to bottom as Size,
  then the Stack. Polish, Relax, Smooth and Balloon are now steps in Tool > Reshape > Stack (Add step > Surface);
  their menu items and keys still run them at 50.
changed: **Tool > Reshape** tidied (foirgairwolf on Discord): **Size**, then the **Stack**, each under its heading.
  The Polish / Relax / Smooth / Balloon sliders and "Smooth keeps volume" above the Stack are removed. Polish, Relax,
  Smooth and Balloon are now steps in Tool > Reshape > Stack (Add step > Surface); their menu items and keys still
  run them at 50. Smooth's step has **Keep volume**; the `deform_polish`,
  `deform_relax`, `deform_smooth` and `deform_balloon` commands are unchanged.
testers:
  - Any OS: Tool > Reshape: Size, then Stack; no sliders above the Stack (Mirror is in Tool > Geometry > Symmetry).
  - Any OS: Stack > Add step > Surface > Smooth, tick Keep volume: the preview follows; Apply, then Ctrl+Z undoes it in one step.
---
Stack kept as the single home: it already had all four Surface operations plus Keep volume, preview, Apply and
Reset, so one place sets an amount for every whole-Part operation; the quick sliders only duplicated part of that.
