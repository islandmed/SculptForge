---
whats_new: **Add to a shelf by touch**: on a tablet, long-press a control, a button or a section's title and choose **Add to My Panel** or a shelf from the small menu. With a mouse, right-click it.
added: **Touch way to add shelf and My Panel items**: hold a finger or the pen still on any control, button or section title for half a second (a ring shows it took) and, as it lifts, a one-level menu offers **Add to My Panel** and the four shelves; the item lands where an Alt-drop at the end would put it. A mouse's right-click opens the same menu on a control with no menu of its own. A slider held this way does not move, and a quick tap on one still sets it.
member_request: true
testers:
  - Android tablet, no keyboard: Tool > Viewport Look, long-press Backface culling; a ring appears, lift, pick Add to the top shelf. The box stays as it was and is now on the top shelf.
  - Same tablet: long-press the brush Size slider (Draw palette) and pick Add to My Panel; the size does not change. Tap the slider quickly; it sets the size as before.
  - A pen: hold the tip still on a button (Subdivide) for half a second, lift, pick a shelf; the button was not pressed.
  - A mouse: right-click a checkbox; the same menu opens (brush tiles and layers keep their own right-click menus).
---
Reviewers: ui::TouchPin (Ui.cpp), SliderInside's hold, App::finishMyPanelDrag, tests/ui/touch_add_to_shelf.sfui.
