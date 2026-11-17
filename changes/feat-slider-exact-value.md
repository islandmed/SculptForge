---
whats_new: **Type an exact value on any slider**: double-click (or Ctrl+click, or double-tap with a pen) any slider or drag number in the panels, the top bar and Brush Lab, type the number and press Enter. Esc cancels, Tab goes to the next field.
added: **Exact values on every slider**: a double-click (Ctrl+click as before, a pen's double-tap too) on any slider or
  drag number opens a typed field: Enter applies, Esc cancels and keeps the value from before the first click, Tab goes
  on to the next field. The Level slider waits out the double-click before it switches, so a double-click switches once, with the typed number. Before, only Ctrl+click
  typed on sliders and a double-click only on drag numbers such as Part size. Requested by a member.
testers:
  - Any OS: double-click Size and Intensity in the top bar, type a number, press Enter; then double-click, type, press Esc (the value stays).
  - Any OS: Remesh palette, double-click Target Quads, type 7000, press Tab (the next field opens), Esc.
  - Pen: double-tap a slider with the pen; the typed field opens.
---
Requested by a member (anonymous). Patch of ImGui's SliderScalar in cmake/Dependencies.cmake; the voxel slider has its own field (PanelRetopo.cpp).
