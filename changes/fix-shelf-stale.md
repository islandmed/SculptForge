---
whats_new: **Brushes you made are safer**: a brush file that can't be read is kept and comes back from its backup; a bottom-shelf slot whose brush is missing shows a "missing" mark instead of a brush that looks usable.
fixed: **A custom brush lost from the bottom shelf** (rand_gogh): a slot whose brush wasn't loaded kept its name and looked usable.
  It now shows a missing mark with a tooltip, finds the brush again if its file comes back (or is in the new backups folder),
  and Remove from shelf clears it.
fixed: **Brush files are saved more safely**: a save that doesn't fully reach the disk no longer replaces the good file, the file being
  replaced and a deleted brush are copied to `backups/` in the brush library first, a brush file that can't be read is kept (and listed)
  and restored from that copy, and brushes that exist only in this session survive a library reload (installing or switching a pack).
testers:
  - Windows: make a brush in Brush Lab, put it on the bottom shelf, close the app, delete its file in the brush library folder, start again; the slot shows a missing mark and the tooltip says why. Put the file back; the brush returns within seconds.
  - Same, but delete the brush from the Brush palette (Delete...): it is in `backups/`; the slot goes missing; right-click > Remove from shelf clears it.
---
Reviewers: BrushLibrary.cpp (writeBrushFile reads back before replacing, backups/, recoverBrush, load keeps Session/Project brushes),
Shell.cpp (missing tile). Root cause for the reporter's file loss is unproven: no file or log from them; these close the silent paths.
