# Testers: Cut, Front faces only, Duplicate Part (0.10.9 preview)

- **Cut, any OS, mouse and pen:** load a sphere or your own sculpt. Hold Ctrl+Shift, open the brush picker and try Cut Line, Cut Lasso, Cut Rectangle, and Cut Polyline (click the points, then click the first one again to close, or double-click to end it open). Try each with Tool > Cut > Away, In and Add. Good: it matches the orange preview, the cap is flat, Ctrl+Z undoes it in one step, and the model stays closed (no holes along the cut).
- **Cut with symmetry on (X) and with two Parts:** only the active Part is cut, and the mirror side matches. Good: no holes, no stray slivers along the cut, and the other Part is unchanged.
- **Windows display scaling 125/150/175 % and a display tablet (Wacom, Huion, XP-Pen):** draw a Cut Polyline with the pen. Good: points land where the pen taps, the close circle around the first point is easy to hit, Enter or a double-tap ends it, and Esc drops it.
- **Masking > Front faces only:** with Mask Lasso, mask across the whole model with the option on, then rotate. Good: the back stays unmasked; with the option off, the back is masked as before.
- **Ctrl+Shift+D:** duplicates the active Part (it shows as "<name> copy" under the original). Good: it doesn't fire while you type in a text field, and Blender-style keys still work.
