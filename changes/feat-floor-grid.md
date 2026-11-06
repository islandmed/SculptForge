---
whats_new: **Floor grid**: **View > Floor grid** draws a faint grid on the floor through the world origin, for grounding,
  perspective and lining up Parts. Off by default.
added: **Floor grid** (#115, **View > Floor grid**, also **Tool > Display** and the command palette): a faint grid on the
  ground plane through the world origin (Y up), X and Z axes marked, squares stepping by ten with the zoom and fading
  into the distance. Off by default; remembered in the preferences. Suggested by @fenixpoints.
testers:
  - Windows at 150% scaling: View > Floor grid; orbit and zoom out and in: the lines stay thin and smooth, the squares
    step without popping, no shimmering near the horizon.
  - Any OS: File > New Plane with the grid on: the plane covers the grid, no flicker. Restart: the grid is still on.
---
The grid sits at the world origin (y = 0), not at the model's lowest point: stable while sculpting, and it is the
plane exports line up to (#115 asks "where the model sits relative to the origin").
