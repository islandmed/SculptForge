---
whats_new: "**Circle shapes**: Circle Mask, Select Circle and Cut Circle draw a circle or ellipse from its centre. Shift keeps it round."
highlight: Circle and ellipse shapes for Mask, Select and Cut
added: "**Circle shape for Mask, Select and Cut** (#213, thanks @dCo3lh0): **Circle Mask** (Tool > Masking), **Select Circle** (Tool > Visibility) and **Cut Circle** (Tool > Cut) sit beside the rectangle and lasso in Tool, the Ctrl and Ctrl+Shift brush pickers and the stroke popup. Drag from the centre; the cursor sets the width and the height, so it can be an ellipse. Shift while dragging keeps it a perfect circle (Select and Cut already hold Shift: let go of it). Alt at the press draws Cut Circle from a corner; **Draw from corner** and **Keep round** in each panel set both for every drag. Symmetry, Mask Strength, Front faces only, the Cut modes and Alt (unmask, hide inside) work as for the lasso."
testers:
  - Any OS, Tool > Masking > Circle Mask: Ctrl+drag from the middle of a head outward; a round patch masks, Ctrl+Alt+drag the same spot unmasks it. Drag wider than tall for an ellipse; press Shift while dragging for a circle.
  - Tool > Visibility > Select Circle, then Ctrl+Shift+drag: only what is inside the circle stays visible; Alt hides the inside instead.
  - Tool > Cut > Cut Circle, mode Away: Ctrl+Shift+drag a circle through a sphere; a round hole with a flat wall. Hold Alt as you press to draw from a corner; try In, Add and Clip.
---
Circle selection for #213. The circle is sent through the lasso commands as a 128-point outline, so nothing new is
exposed to scripts. Coordinate with the Space-reposition branch (#214): it moves the press and the cursor together, which
the circle follows without changes.
