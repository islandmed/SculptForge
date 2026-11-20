---
whats_new: **Clay at Micro level 4**: a long Clay stroke on a dense Micro level keeps up with the pointer much better; the lighting under the brush catches up in the following frames.
fixed: **Long Clay strokes on a Micro level 4 mesh lag and stall** (#184, thanks @HG4Science): the shading of the tiles under the brush was recomputed in full every frame, the same tiles again and again. It now runs within a per-frame time budget, each changed tile queued once, and finishes after the stroke (the same result as before, checked against a full re-shade).
testers:
  - Open a 2.5M base mesh at Micro level 4, hold a big Clay brush down for 20 s while sweeping over the face: the cursor should stay close to the surface change, no multi-second freezes.
  - Stop the stroke: the shading near the brush finishes within a second or two, no dull patch stays. Orbit right after: smooth lighting everywhere.
---
Follow-up to the 0.13.9 Focus Detail work. Cause (v0.13.9, b2.5m-m4, floor tier, 25 s Clay sweep): per frame, 40-130 ms of
main-thread coarse shading of the tiles under the brush (plus their two rings of neighbours), re-done every frame for the same
tiles, on top of Focus Detail's scoring. Reporter's freeze (73 px brush, one 2-3 s stall, one 277 ms dab batch): overload of
the main thread by this work. Test: hw_stroke_shade_budget (coarseShadeBatch bound).
