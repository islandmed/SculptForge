---
whats_new: **My Panel takes single controls**: hold <kbd>Alt</kbd> and drag any slider, checkbox, choice or readout (**Backface culling**, the brush **Size**, the triangle count, **Edit > Preferences > Input** options) from any palette into **Edit > My Panel**, next to the buttons and sections it already holds. Reorder and remove them the same way; saved with the layout; in both setups.
highlight: Any control in My Panel
added: **My Panel**: single controls (Edit > My Panel; requested by Hameron and ebarranco on Discord). Hold Alt and drag a
  slider, checkbox, choice or readout from any palette into My Panel, Classic or Streamlined setup: it is the palette's
  own control, live, so a change in My Panel is a change everywhere. Alt+drag to reorder, drag out or right-click to
  remove, saved with the layout. A control its palette shows only sometimes (the Levels line of a Part without levels)
  shows as its dim name until it is back. While Alt is held a slider is picked up, not changed. The triangle count
  (Tool) and the Levels line (Tool > Geometry) can be pointed at with Ctrl for help, and dragged, like any control.
  The floating My Panel (its key) is now as tall as what it holds, up to 70 % of the view, instead of a fixed tall window.
member_request: true
testers:
  - Any OS, Classic setup: Edit > My Panel..., then hold Alt and drag Tool > Viewport Look > Backface culling into it;
    tick it in My Panel, the back faces disappear in the view and the Tool palette's box ticks too.
  - Hold Alt and drag the Draw palette's Size slider into My Panel: the brush size did not change during the drag; drag
    the copy in My Panel, the brush ring follows.
  - Edit > Preferences > Input: Alt+drag Orbit around the pointer and the Rotation choice into My Panel; the choice opens its
    list from My Panel.
  - Restart: the controls are still in My Panel, in the same order. Streamlined setup: the same drags work.
---
Control entries are "control:<scope>/<key>" (UiLayout.h): scope = section id or palette id, key = the control's help
title (ui::HelpText) or action id (ui::HelpAction). My Panel draws the control's section cropped to that control
(PanelMyPanel.cpp drawMyPanelControl), so it is the real widget. Tests: tests/ui/my_panel_controls.sfui,
tests/ui/sizes/my_panel_controls.sfui, test_shell_layout my_panel_layout_json.
