---
highlight: Side shelves no longer scroll sideways (thanks dm4nd on Discord), and Frame fits the model into the free area of the canvas
fixed: The side columns (the shelf with Undo and Redo, the brush and colour shelf) and the trays no longer scroll
  sideways. At some UI scales their tiles were a few pixels wider than the column, so a sideways swipe or scroll
  moved them; the tiles now fit the column and the trays only scroll up and down (dm4nd on Discord).
testers:
  - Windows, 1920x1200 fullscreen, display scaling 100 %, 125 % and 150 %, UI scale Auto: open Tool > Remesh in the
    right tray, swipe sideways on a trackpad (or Shift+wheel) over the tray and over the Undo/Redo column; nothing
    moves sideways and no tile is cut off at the edge.
---
