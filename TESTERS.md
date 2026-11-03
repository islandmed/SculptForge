# Testers (0.10.9): experimental performance toggles (Preferences > Performance, both off by default)
- **Live Move**: turn it on, use the Move brush on a dense model (Help > Benchmark's head, or anything over ~10M triangles), drag big and fast. The drag should follow the pen smoothly; after you lift, the shape must stay exactly where the preview left it (no jump). Try with X symmetry, then undo / redo.
- **Live Move fallbacks**: with a mask, hidden parts, a sculpt layer or a micro level, Move should behave exactly as before (no preview, no errors).
- **Background Preview**: turn it on, sculpt with a 200-300 px Clay / Standard brush on a 15M+ model. The stroke should stay under the pen instead of trailing; lifting the pen should be instant and the final result look the same as with it off.
- Report: GPU and driver, triangle count, anything that flickers, vanishes for a frame, or looks different after the pen lifts (a screenshot of the area helps).
