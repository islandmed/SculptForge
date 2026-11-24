---
added: "Brush cursor: a note appears under the cursor when Shift (or the Smooth brush) would seem to do nothing: \"Smooth face group borders only\" when that Brush option is on for a mesh without face groups, and \"Shift smooths the colour; Sculpt mode smooths the shape\" when Shift is held in Paint mode."
testers:
  - "Any OS: in Brush, tick Smooth face group borders only on a plain sphere, hold Shift over it: the note shows under the cursor, and Shift moves nothing (as before). Untick it: no note, Shift smooths."
  - "Any OS: switch Draw to Paint and hold Shift over the model: the note says Shift smooths the colour; switch Draw to Sculpt: no note and Shift smooths the shape. Also check with Front faces only on: both notes stack under the cursor without overlapping."
---
Reviewers: src/app/App.cpp (cursor tags loop; same place as the Front faces only tag), UiTest values smooth_borders_tag / smooth_colour_tag. Tests: tests/ui/smooth_option_tags.sfui (tags and the no-op) and tests/ui/shift_smooth_{brushes,options,surfaces}.sfui (Shift smooths in every brush, input style, falloff option and surface kind).
