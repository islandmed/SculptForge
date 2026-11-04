---
fixed: **Move with Pressure > Size on a pen** (#122, reported by Zoldyck-Kill): Move and the Kelvinlet grab
  brushes set the area they grab at the press, when a pen barely touches, so with Pressure > Size on they grabbed
  only a few percent of the brush and seemed not to work. They now always grab with the full brush size;
  Pressure > Strength still applies.
testers:
  - Windows + pen tablet: turn on Pressure > Size, pick Move, press lightly and drag; the whole brush circle moves.
  - Same with KelvinGrab.
---
