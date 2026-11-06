---
whats_new: **Line Gradient**: **Color > Line Gradient**, then drag across the model: the paint colour runs into a second colour (or fades out) along the line; drag its ends to adjust it. Thanks @Devon0209 (#99).
added: **Line Gradient** (Color, #99, requested by @Devon0209): a screen-space colour ramp for vertex paint and textures,
  its ends adjustable before you keep it (Enter; Esc takes it back); respects the mask, one undo step. Script: `paint_gradient`.
testers:
  - Color > Line Gradient, drag across a sphere: the paint colour runs into the End colour (black); drag an end handle:
    the ramp follows. Ctrl+Z once removes it.
  - Mask the top (Ctrl+drag), tick Clear, drag again: the masked part keeps its paint, the rest fades out along the line.
  - In a texture (Tool > Texture Map > New Texture): the ramp is smooth across the texture.
---
