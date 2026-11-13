---
whats_new: **A brush on your shelf stays on your shelf**: rename the brush, sort its file into a folder, or change its name in the file, and the shelf slot still finds it.
fixed: A bottom-shelf slot for one of your brushes no longer shows "missing" while the brush file is still on disk (rand_gogh on Discord). Each brush file now carries a hidden id, and the slot follows that id, so renaming the brush, moving its file into a folder inside the brush library, or a change of upper/lower case in its name no longer breaks the slot. Brush files from older versions get their id the first time the library loads (the file is saved again, with a copy in backups/). A brush you really deleted still shows "missing" and the slot stays until you remove it.
testers:
  - Any OS: put one of your brushes on the bottom shelf, rename it in the Library (right-click > Rename); the shelf tile changes its label and still picks the brush.
  - Windows: put a brush on the shelf, close SculptForge, move the .sfbrush into a new folder inside the brushes folder, start again; the slot finds it.
  - Delete the brush in the Library; the slot shows the missing mark and stays.
---
Core: id in .sfbrush (library-written, never in brush codes), UiLayout.brushIds, relinkBrush.
