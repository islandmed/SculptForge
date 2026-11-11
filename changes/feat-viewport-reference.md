---
whats_new: **Reference images**: **View > Reference images** keeps pictures in the view while you sculpt: behind
  the model, or as planes at its front, side or top that turn with the view. **Front + side...** sets up a
  character's two drawings in one go. They never take strokes and stay out of renders unless you ask.
highlight: Reference images to sculpt against
added: **Reference images** (View > Reference images; idea by Oslen on Discord): add one or more PNG or JPEG
  images (or drop them on the window while the section shows). Each sits **Behind the model** (fixed to the view)
  or **In the scene** (a plane at the front, side or top that turns with the view and fades out edge-on), with
  opacity, flips, lock and hide; **Adjust** moves it and scales it from its corners (Shift keeps its proportions).
  **Front + side...** makes a front and a side plane from two files in one click. They never take strokes or picks
  and are left out of renders, exports, thumbnails and the timelapse unless **Show in renders** is on. Projects
  keep them, embedded or by path; images over 4096 px are scaled down, files over 64 MB, 64 megapixels or 8192 px a side
  refused (read from the header, before decoding), 8 per project. An image a project names by path opens linked,
  with Embed off, so saving never copies a file from your computer into it unless you tick Embed.
  Scriptable: `reference_add`, `reference_set`, `reference_remove`, `reference_pair`.
testers:
  - Windows at 150% scaling: View > Reference images > Add images..., pick a photo. It shows behind the model; turn
    the view, it stays put. Sculpt over it: the strokes land on the model as before.
  - Any OS: Front + side..., pick a front then a side drawing. Numpad 1 shows the front behind the model, Numpad 3
    the side; orbit between them, each fades out edge-on.
  - Any OS: Adjust: drag a corner (each way on its own), then with Shift (keeps the shape); Enter ends. Save the
    project, open it again: the images are back where you left them.
  - Any OS: Render > Materials > Save Render with Show in renders off: no reference in the image. On: it is there.
  - Base-spec / integrated GPU: two 4K images, orbit: as smooth as without them.
---
Core: src/core/Reference.h. Renderer: Renderer::setReferences.
UI: src/app/ReferenceImages.cpp. Tests: tests/test_reference.cpp, tests/ui/reference_images.sfui.
