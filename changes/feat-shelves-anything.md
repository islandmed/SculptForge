---
whats_new: **Shelves for every option**: hold <kbd>Alt</kbd> and drag any checkbox, slider, choice, readout or button from any palette (**Backface culling**, the brush **Size**, **Edit > Preferences > Input > Orbit around the pointer**, **Subdivide**) onto any of the four shelves around the canvas, no Customize mode needed; a line shows where it lands. It stays the palette's own control, live. Rearrange with Customize Shelves as before, right-click to remove, saved with the layout, in both setups.
highlight: Any option on a shelf
added: **Shelves**: any control, any shelf (requested by members). Hold Alt and drag a checkbox, slider, choice, readout or
  button from any palette onto the top, left, right or bottom shelf, Customize Shelves off or on, Classic or Streamlined
  setup: a line shows where it lands, and it is the palette's own control, live, so a change on the shelf is a change
  everywhere. While Alt is held a slider is picked up, not changed. On a side shelf a control takes the column's width (drag the left shelf's inner edge wider for a long label); on
  the top or bottom shelf it is as wide as it is in its palette, centred in the row. Ctrl+Alt in Customize Shelves moves it
  along or between shelves and off them, as for every item; right-click > Remove from shelf; saved with the layout. A
  control its palette shows only sometimes is hidden on the shelf until it is back (shown dim in Customize Shelves so it can
  be removed). The same drag puts it into My Panel, and an entry dragged out of My Panel onto a shelf moves there.
member_request: true
testers:
  - Any OS, Classic setup: hold Alt and drag Tool > Viewport Look > Backface culling onto the top shelf (an orange line shows the
    spot); tick it on the shelf, the back faces disappear and the Tool palette's box ticks too. Untick it in the palette: the shelf
    follows.
  - Hold Alt and drag the Draw palette's Size slider onto the bottom shelf: the brush size did not change on the way; drag the
    shelf's copy, the brush ring follows.
  - Edit > Preferences > Input: Alt+drag Orbit around the pointer onto the left shelf. Tool > Geometry: Alt+drag the Subdivide
    button onto the right shelf, it is a tile like the others.
  - Turn on Customize Shelves (Preferences > Saving & Layouts) and Ctrl+Alt+drag the shelf's Backface culling to another shelf;
    right-click it > Remove from shelf. Restart: the rest is still on the shelves. Streamlined setup: the same drags work.
---
Shelf items "control:<scope>/<key>" are the My Panel control entries (UiLayout.h, feat/my-panel-controls); shelves accept the
Alt-drag payload (Shell.cpp drawShelf) and draw a control through the shared PanelMyPanel.cpp drawPinnedControl (host = the
shelf; place cache per host). Tests: tests/ui/shelves_anything.sfui, tests/ui/sizes/shelves_anything.sfui, test_shell_layout
my_panel_layout_json. Built on feat/my-panel-controls 920289d3e (merged into this branch): merge that first.
