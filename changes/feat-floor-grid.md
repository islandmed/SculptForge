---
whats_new: **Floor grid**: **View > Floor grid** draws a faint grid on the floor the model stands on, for grounding,
  perspective and lining up Parts. Off by default.
added: **Floor grid** (#115, **View > Floor grid**, also **Tool > Display** and the command palette): a faint grid on the
  floor under the model (its lowest point, Y up; it moves when a stroke or a move ends), the X and Z axes through the
  origin marked, squares stepping by ten with the zoom and fading into the distance. It never draws over the model.
  Off by default; remembered in the preferences. Suggested by @fenixpoints.
testers:
  - Windows at 150% scaling: View > Floor grid; orbit and zoom out and in: the lines stay thin and smooth, the squares
    step without popping, no shimmering near the horizon, and the sphere stands on the grid.
  - Any OS: sculpt a long stroke down past the bottom of the model: the floor moves once the stroke ends, not during
    it. Orbit below the floor: the model shows whole, the grid only around it. Restart: the grid is still on.
---
The floor follows the lowest point of the visible Parts (measured when the history or the Parts change, not during a
stroke or a drag); it is drawn before the Parts, so it never covers mesh pixels. Coordinator review 11-06.
