---
added: **Tube**: **S** sets the tube's size at the cursor, as it does a brush's: with **Draw Tube** on it sets
  **Size** for new tubes (a ring shows it), and with a tube's point selected it sets that point's **Radius** (a ring
  around the point; one undo step). Requested by GABO on Discord.
testers:
  - Any OS: Tool > Tube > Draw Tube, press S over the canvas and drag: the ring grows, and the next tube is thicker.
    The brush size is unchanged after leaving Draw Tube.
  - Any OS: Shift+drag a tube's dot (it turns orange), press S and drag: the ring around the dot grows; on release the
    tube is thicker there, and one Ctrl+Z puts it back.
---
