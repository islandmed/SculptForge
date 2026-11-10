---
whats_new: **Front faces only** now shows when it is on: a small tag by the brush cursor and a line at the top of the Brush palette, with a button to turn it off.
changed: **Front faces only** (Brush palette) is easy to leave on, and then Smooth and every other brush skip the inner side of a thin or Extract-made Part, so spikes inside look unreachable. While it is on, the brush cursor carries a "Front faces only" tag and the Brush palette starts with "Front faces only is ON" (a tooltip says it skips hidden and inner surfaces) and a Turn off button. Reported by rand_gogh, who found Shift-smooth "does next to nothing" until the option was unticked.
testers:
  - Any OS: Brush palette > tick Front faces only; the brush cursor shows a "Front faces only" tag under the ring and the palette starts with "Front faces only is ON". Hover the line for the tooltip; Turn off clears both.
  - Windows: Extract a Part (Tool > Part > Extract), spike its inside with Clay, tick Front faces only: Shift-smooth leaves the inside as it was (the tag tells you why); untick it and Shift-smooth takes the spikes down.
---
Guard test: tests/test_smooth_spikes.cpp (Smooth reaches both layers of an Extract shell; Front Faces Only skips the inner one). No smoothing bug: Shift-smooth with Dots was fine (6.93M triangles).
