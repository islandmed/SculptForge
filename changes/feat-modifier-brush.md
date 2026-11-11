---
whats_new: **Shift and Ctrl brushes**: **Shift+click** a brush on the bottom shelf (Trim, Flatten...) and holding Shift sculpts
  with it instead of Smooth; **Ctrl+click** one and holding Ctrl sculpts with it instead of masking. Your main brush stays.
highlight: Put any brush on Shift or Ctrl
added: **Shift and Ctrl brushes**: Shift+click a brush on the bottom shelf (or in the brush picker) to make it the Shift
  brush, Ctrl+click one on the shelf for the Ctrl brush; the main brush stays. Each keeps its own Intensity, the tile shows
  a Shift / Ctrl badge and the status line names it. Shift+click Smooth, a mask brush picked with Ctrl held, or **Reset
  Shift brush** / **Reset Ctrl brush** in the command palette go back; Smooth's alternate smooth (#40), Ctrl+Alt unmask,
  the background mask rectangle and Ctrl+Shift are unchanged. Kept in the preferences; scriptable with
  `set_modifier_brush shift=trim ctrl=flatten`. Asked for by Cytrick on Discord.
testers:
  - Any OS: click Standard, then Shift+click Trim on the bottom shelf. Status says "Shift brush: Trim", Trim gets a "Shift"
    badge, Standard stays the active brush. Shift+drag on the model trims; a plain drag is Standard.
  - Hold Shift and look at the Intensity slider: "Trim Intensity (Shift)"; change it, let go, the main Intensity is unchanged.
  - Ctrl+click Flatten: Ctrl+drag on the model flattens (no mask). Ctrl+Alt+drag still erases a mask; Ctrl+drag beside the
    model still draws the mask rectangle.
  - Ctrl+B with Ctrl held, pick Mask Pen: Ctrl masks again. Shift+click Smooth: Shift smooths again, and letting go of Shift
    mid-stroke still switches to the volume-keeping smooth.
  - Restart: the Shift / Ctrl brushes are kept.
  - Windows at 150-200% scaling: the Shift / Ctrl badges sit in the tile's top-right corner and don't cover the name.
---
Request: Discord #ideas 1568645496283603014 (Cytrick). Ctrl+click on a shelf brush used to just pick it (same as a click,
with the Ctrl-hover help showing); it now sets the Ctrl brush. Brush tiles' Ctrl-hover help mentions both clicks.
