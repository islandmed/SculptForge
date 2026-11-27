---
fixed: On a touch screen or with a pen, dragging a slider (Exposure, Roughness, Metallic and the rest) could freeze after half a second and open the Add to My Panel / shelf menu over the drag. A touch on a slider is now always a drag; the long-press menu is for buttons, boxes and section titles.
testers:
  - Android tablet, finger and pen: drag any slider (Lit Sphere Exposure, Roughness, Metallic, brush size), fast and very slowly, and rest a finger on one for a second; the value always follows the finger and no menu ever opens. Long-press a button (Remove Hidden) still opens the menu.
  - Anyone with a keyboard: Alt-drag a slider onto a shelf still works.
---
Branch fix/touch-pin-slider-drag. Cause (since 0.14.1): the long press (ui::TouchPin, kLongPress 0.5 s) took a press that stayed within 8 px for half a
second and cancelled the slider's drag; a slow drag, or one whose moves arrive late, looked like a hold. A first try with a 1.5 px slop for sliders
still failed on a device (ebarranco, 11-27). Now a slider or any drag/typed-value box (ImGui's Inputable item flag, kept by the patched imgui_widgets.cpp, read by ItemIsValueBox in Ui.cpp) never
starts a hold, so the two cannot conflict whatever the touch events look like. Touch pinning of a slider needs a pin handle (not built).
Tests: tests/ui/touch_slider_drag.sfui (verb touch_slide), touch_add_to_shelf.sfui.
