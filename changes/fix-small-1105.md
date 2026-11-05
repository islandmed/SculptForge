---
whats_new: **AI off means off in the menus too**: with **Edit > Preferences > AI > Enable AI features** off, the
  Mesh menu's section is **From Image** (Relief, Inflate, Stamp) instead of Generate 3D.
whats_new: **Voxel Remesh up to 512**: the **Tool > Remesh** Resolution slider now goes from 32 to 512, as the
  `remesh` command does.
whats_new: **A pen resting on the tablet no longer blocks the mouse or trackpad**: lay the pen down and the cursor,
  the shelf buttons (the mirror's X too) and the menus answer the mouse at once.
fixed: With **Edit > Preferences > AI > Enable AI features** off, the Mesh menu still showed a **Generate 3D** section; it
  is now **From Image** and holds only the offline Relief, Inflate and Stamp (the AI entries were already gone).
  The status messages after opening an image point there too (reported by ebarranco on Discord).
fixed: A pen lying still in range (laid on the tablet, or held still above it) kept sending a hair of tilt and
  sub-pixel motion, and each event counted as the pen in use: the mouse and trackpad could neither move the cursor
  nor click for as long as it lay there, and the pen pulled the cursor back to itself. With a Wacom, a SpaceMouse
  and a trackpad on a Mac the window seemed frozen and the mirror's X out of reach (reported by phoenix on
  Discord). Only touching, buttons, coming into range, pressure and real movement count now.
changed: **Tool > Remesh > Voxel Remesh**: the Resolution slider goes from 32 to 512 (was 64 to 400), as the
  `remesh resolution=` command does (asked by fxrtst on Discord). 512 is safe on 8 GB: a 28M-triangle scan in
  about 8 s (14 s on 4 cores) into about 760k triangles; a round 2M sculpt in about 3 s into about 2.5M triangles,
  2.2 GB at the peak.
testers:
  - Any OS: Edit > Preferences > AI > Enable AI features off; open the Mesh menu: a From Image section (Relief,
    Inflate, Stamp), no Generate 3D anywhere (menus, shelves, the F3 search). On again: Generate 3D is back.
  - Any OS: Tool > Remesh: drag Resolution to 512 and Voxel Remesh a sculpt; it finishes in seconds, the app stays
    responsive.
  - macOS / Windows with a tablet: sculpt a few strokes with the pen, lay the pen on the tablet (still in range),
    then use the mouse or trackpad: the cursor follows it within a second and clicking X on the shelf toggles the
    mirror. Pick the pen back up: it takes the cursor again at once.
---
Reviewer notes:
- Mirror freeze review (phoenix): the symmetry toggle is a plain flag; strokes copy it per batch (AsyncStroke
  dabSettings), fused batches group dabs by symmetry (SculptBatch batchable), Move/Grab keeps the mirrors it began
  with. The only wait in a stroke is StrokeSolver::wait (logs every 2 s since 0.11.1). No lock found that several
  devices could trigger. What did block input: strayMouseMotion's 1 s "pen in use" window was restarted by every pen
  event, including a resting pen's noise, and an accepted mouse move ended the pen's proximity, so the next noise
  event re-claimed the cursor. tests/ui/mirror_stress.sfui (pen_rest on) fails at line 61 without the fix.
- lastPenNs_ now counts on clickClockNs() (SDL ticks in the app, the script's time in UI tests).
