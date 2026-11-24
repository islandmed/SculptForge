---
whats_new: **Remesh and Remove Hidden one click away**: right-click the canvas (or press <kbd>Space</kbd>) for Quick Remesh,
  Quad Remesh and Remove Hidden; <kbd>Shift</kbd>+<kbd>R</kbd> remeshes without a visit to Tool > Remesh, and
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>H</kbd> removes the hidden part. Cancel stops a Voxel Remesh at once.
highlight: Remesh one right click away; Cancel answers at once
changed: **Quick Remesh is on by default**: <kbd>Shift</kbd>+<kbd>R</kbd>, and <kbd>N</kbd> held while clicking or dragging on
  the background, remesh the active Part at the Voxel Remesh resolution in the background without first ticking Tool >
  Remesh > Quick Remesh (the checkbox turns them off). Reported by a Founding Member: remeshing mid-sculpt took too many
  clicks, and a slow remesh was cancelled instead.
added: **Remove Hidden** is an action: <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>H</kbd> in the SculptForge key style (Preferences >
  Hotkeys changes it; the Classic, Maya and Blender styles keep their keys as they were), in the command palette, and
  as a Quick Menu button.
added: **Quick Menu** (right click on the canvas, or <kbd>Space</kbd>): a third row with Quick Remesh, Quad Remesh and Remove
  Hidden, in the SculptForge layout (the Classic layout's menu is as it was).
testers:
  - Any OS, SculptForge layout: right-click the canvas beside the model; the Quick Menu's bottom row has Quick Remesh, Quad
    Remesh and Remove Hidden. Quick Remesh starts a remesh in the background (the status bar counts up); undo brings the mesh back.
  - Any OS: hide half the model (Ctrl+Shift+drag), press Ctrl+Shift+H; the hidden half is gone, Ctrl+Z brings it back.
    Classic sculpting keys: Ctrl+Shift+H does nothing (Preferences > Input).
  - Any OS: Tool > Remesh on a 2M-triangle sculpt, press Cancel right after Remesh; the Part is unchanged and the status bar
    says cancelled within a blink.
member_request: true
member_credit: Rand_gogh
---
Rand_gogh (Founding Member, Discord): remesh mid-sculpt took long enough to cancel and work around; Remesh and Remove Hidden
were too deep in the menus. Also TETSUYA (member) names Quad Remesh as a reason to be here. Measurements in the branch's report.
