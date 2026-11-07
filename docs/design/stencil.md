# Stencil (#155)

Ask (@kenttumanda123-netizen): a reference image over the viewport that you move, turn, scale and fade, then sculpt
or paint through it: its detail goes onto the model as displacement (sculpt brushes) or colour (Vertex Paint); move
it and repeat. Roadmap item "stencils" (welcome.md). Our name: **Stencil**.

## Model

- **Screen space.** The stencil is an image placed on the canvas, not on the model: the model turns under it. Its
  placement is kept in canvas fractions (centre 0..1 across and down, size = its height / the canvas height, angle
  counter-clockwise as seen), so it stays put when the window changes size. `core/Stencil.h`, state on `Document`
  (`doc.stencil`), not saved with the project (a session tool, like the brush cursor).
- **Sampling.** Each dab carries a `StencilView` (the stencil, the dab's view-projection and canvas aspect). A vertex
  is projected to the canvas, turned back by the stencil's placement into image coordinates and sampled bilinearly.
  Sculpt brushes multiply their falloff (or alpha) by the image's brightness x its alpha; outside the image (unless
  **Tile**) the weight is 0, so nothing moves. Behind the camera: 0.
- **Vertex Paint** lays the image's own colours (alpha = coverage), or with **Paint its colours** off its brightness
  masks the paint colour. Texture painting doesn't take it yet (follow-up: the same weight in `Texture.cpp`).
- **Invert** uses the negative; **Opacity** only changes how strongly the overlay shows (the brushes always take the
  whole image: a faint stencil shouldn't sculpt faintly without saying so; Intensity does that).
- **Symmetry: yes, mirrored.** A symmetry copy's vertex takes the stencil at its mirror image's place on screen
  (`SymCopy::inversePoint` before projecting), so the pattern mirrors across: a face stencilled on the right shows on
  the left mirrored, as the stroke itself is. (Sampling the mirror's own screen position would put a different part
  of the image there, or nothing.)
- **Which brushes.** Every brush that takes an alpha: the built-in sculpt brushes (`Sculptor::dabSingle`), custom
  brushes (`dabRecipeSingle`), Vertex Paint (`paintDab`). Not Move, the Kelvinlet brushes, Vector Displacement or the
  mask brush (they don't take alphas either). A stencilled dab takes the exact path (no scale-adaptive dabs, no fused
  batches, no micro base dabs), as an alpha does.
- **Micro levels:** through the grid kernel's per-point pattern hook (`MicroStroke::dab`'s `texture`), as recipe
  patterns are; big brushes evaluate it on the coarse points the way they do an alpha.

## Image

The same loader as alphas (`Image::load`; no new parser or dependency), kept up to 2048 px on its longer side (a
stencil spans the view; an alpha's 512 is too coarse). **Use alpha** makes the brush's current alpha the stencil: a
Library or image alpha is read again from its file at full size, a built-in pattern as generated.

## UI

Alpha palette > **Stencil**: Load image..., Use alpha, Remove; Show, Adjust, Invert, Tile, Paint its colours, Hide
while navigating; Opacity, Size, Angle; Reset placement. Commands `stencil_load`, `stencil_set`, `stencil_clear`
(scripts, tests, the AI). Actions in the command palette: Load Stencil..., Show Stencil, Adjust Stencil (no default
keys: every input style keeps its own).

- **Overlay** on the background draw list (under the panels, over the model), outline always, image at Opacity.
- **Adjust** puts handles on it: drag inside = move, a corner = scale about the centre, the round knob above the top
  edge = turn (Shift: 15 degree steps). Keys while adjusting: arrows move (Shift: 1 px), `-` / `=` scale, `,` / `.`
  turn 15 degrees (Shift: 1). Enter or Esc goes back to sculpting. While adjusting the brush ring hides and clicks off
  the stencil do nothing.
- **Navigating** (orbit, pan, zoom, roll) fades it to 15% (Hide while navigating, on by default): it stays locked to
  the view, which is what "reposition and repeat" needs: turn the model under it, stroke again.

## Cost

No stencil: one null check per dab. With one: a matrix multiply and four texel reads per vertex in the brush. The
stroke lag bench (25/120/240 px) runs without a stencil and is unchanged.
