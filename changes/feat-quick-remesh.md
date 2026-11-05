---
whats_new: **Quick Remesh**: turn it on in **Tool > Remesh**, then press **Shift+R**, or hold **N** and drag on the
  background, to remesh the active Part in the background while you keep sculpting. **Remesh after each stroke**
  does it by itself when a stroke stretched the surface.
highlight: Remesh with one key, or after each stroke
added: **Quick Remesh** (Tool > Remesh, #121, thanks Zoldyck-Kill): with it on, Shift+R, or N held while clicking or
  dragging on the background, remeshes the active Part at the Voxel Remesh resolution in the background (progress
  bar and Cancel as the button; Keep Masked and Keep Texture apply). One undo step per remesh; a stroke or undo made
  while it runs is never overwritten (it remeshes again). Both keys are free in every key style and change in
  Preferences > Hotkeys; on the model a press stays a brush stroke.
added: **Remesh after each stroke** (Tool > Remesh, off by default, #121): after pen-up, a stroke that stretched
  edges past 1.6 times their length remeshes the active Part in the background; sculpting never waits for it. Not
  on Parts with subdivision levels, sculpt layers or a painted texture.
testers:
  - Any OS: Tool > Remesh > Quick Remesh on; sculpt, press Shift+R and keep sculpting. The status bar counts up,
    the remesh lands when you lift the pen, Ctrl+Z undoes just the remesh.
  - Hold N and drag on the background in each key style (Preferences > Input): it remeshes, the view doesn't turn.
    Without N the drag turns the view as before; with N on the model it is a normal stroke.
  - Remesh after each stroke on: a long Move pull remeshes after pen-up; small Standard strokes don't.
  - Windows, pen: the same with the pen tip on the background while N is held.
---
Shift+R and N were the only free keys in all four styles (Classic, SculptForge, Blender, Maya): every modifier
combination of a left drag on the background is already taken (Ctrl mask shapes, Ctrl+Shift hide shapes, Alt pan,
Shift snap/roll, Blender Shift/Ctrl/Alt navigation, Maya Alt navigation), so the drag uses a held key instead.
