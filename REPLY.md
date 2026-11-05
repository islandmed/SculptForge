# Reply draft for #136 (not posted; post at the release that ships feat/vdm-rotation, then close)

Vector displacement stamps can now be rotated (out now, in the latest update). With the VDM brush picked, **Sculpt > Brush > Vector Displacement** has a **Stamp rotation** section under the map's preview:

- **Follow stroke direction**: on by default, so Drag Stamp turns the stamp with your drag as before. Turn it off and the drag only sets the size: every stamp keeps the fixed angle, however you drag.
- **Angle**: a fixed turn, -180 to 180 degrees. It works on its own or on top of the drag's direction. Slide Stamp uses it too.
- **Angle jitter**: each stamp gets a random turn, up to that many degrees either way. Handy for scattered horns, rivets or scales that shouldn't all face the same way.

The VDM brush keeps its own settings, separate from the alpha brushes, and **Save Brush Settings** keeps them for next time.

On sharing: brush files used to keep only Follow stroke direction. Now **Save Brush Settings** on a custom brush also writes its alpha **Angle** and **Angle jitter** into the brush file, so a brush you share as a brush code or in a .sfpack turns its alpha the same way for whoever installs it.

Guide: https://sculptforge.app/guide/sculpting.html#stamp-rotation

Thanks for the request!
