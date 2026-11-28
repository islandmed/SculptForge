---
whats_new: "**Saving & Layouts, safer**: **Edit > Preferences > Saving & Layouts** is now three blocks (My layout, Share, Start over). Every reset asks once and keeps what it replaces; **Undo last reset** puts it back, even after a restart."
highlight: Resetting the layout can't wipe your setup any more
fixed: "**Saving & Layouts**: a member who tried to get their custom interface back ended up resetting it. The page is grouped into **My layout** (**Save my layout**, **Restore my saved layout** with the save time, greyed with \"Nothing saved yet\" until you have saved one), **Share** (**Export layout file...**, **Import layout file...**) and **Start over** (**Reset panels and shelves**, **Reset layout and shortcuts**, **Reset panel windows**). Each reset asks \"This replaces your current layout. Your current one is kept as a backup.\" and writes the backup (`ui.reset-backup.json`) before it changes anything, and **Undo last reset** restores exactly that: layout, shortcuts, radial menus and panel windows, also after a restart. Pressing Reset twice keeps the first backup. **Restore my saved layout** keeps what it replaces too. Commands and shortcut bindings keep their ids."
testers:
  - Any OS, Classic and SculptForge layout: close a tray, rebind a key, pop a palette into its own window; Edit > Preferences > Saving & Layouts > Reset layout and shortcuts, Replace; then Undo last reset brings all three back. Restart first and try again.
  - Fresh settings: Restore my saved layout is greyed and the page says "Nothing saved yet".
  - macOS: Save my layout, change the layout, quit and reopen, Restore my saved layout returns the one you saved (not the built-in one).
---
Reviewers: the Reset buttons now open a modal (App::drawResetConfirm); renders and recordings run it directly.
Backup logic in Config.cpp (backUpBeforeReset / undoLastReset); a failed backup write aborts the reset.
