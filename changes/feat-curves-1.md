---
whats_new: **Curves**: **Tool > Tube > Result > Curve** draws a guide line on or over the model; **Resample** sets its point count exactly and **Tube from curve** turns it into a tube. New tube profiles **Rope**, **Braid** and **Sleeve**, and **Twist**, **Spin** and **Spread** sliders.
highlight: Curves, and rope, braid and sleeve tubes
added: **Curve Part** (Tool > Tube > Result > Curve; `tube_new curve=true`): a guide line drawn on or over the surface, with editable points, **Resample** (`tube_resample count=N`, exactly N evenly spaced points) and **Tube from curve** (`tube_from_curve`), which uses the Tube tool's sweep. New Tube profiles **Rope** and **Braid** (three strands round or across the curve) and **Sleeve** (hollow, open ends), and the **Twist**, **Spin** and **Spread** modifiers (`twist`, `spin`, `spread` in `tube_new`, `tube_set`). Requested by Hameron (FXiT), GABO and Artizmo.
member_request: true
testers:
  - Any OS: Tool > Tube > Draw Tube, Result > Curve, drag over the model; drag the Resample slider to 8, then press Tube from curve. Good = a tube along the line, undo brings the curve back.
  - Any OS: pick Rope, Braid, Sleeve on a drawn tube and move Twist, Spin and Spread; the tube follows live and stays closed.
---
Part 1 (0.12.3). Part 2 (own branch): curve from edge loop, Bezier/NURBS, repeat a piece along the curve.
