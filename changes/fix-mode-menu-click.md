---
fixed: With a menu or pop-up open (a palette from the top menu or a task menu, the command palette, a right-click menu),
  a click on a shelf brush, a tool button or a stroke on the model now does what it says and closes the menu; before,
  the first click (and with the top menus every click on the brush shelf) only closed it, or did nothing.
testers:
  - Classic layout: open Color from the top menu, click Clay on the brush shelf: the menu closes and Clay is picked.
  - SculptForge layout: open Paint > Color, drag on the model: it sculpts and the menu closes.
  - Any OS: press F3, then click Clay on the brush shelf: the palette closes and Clay is picked.
---
Found by the mode matrix (tests/ui/mode_matrix.sfui); its modes-menu-click and command palette known lines are deleted.
