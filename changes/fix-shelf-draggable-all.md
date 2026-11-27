---
whats_new: **Every control on a shelf**: **Remove Hidden** and about 45 more buttons, sliders and boxes (Face Groups, Masking, Parts, Color, Export, Visibility) can now go on a shelf or My Panel with Alt+drag or a long press, like the rest.
fixed: **Remove Hidden** (Tool > Visibility) and some 45 other controls (Show all, Invert visible, Delete masked, the Face Groups buttons, the Masking sliders, the Parts buttons, Fill, Cavity, the colour picker, the LOD and Unreal export controls, Autosave, the hint switches and more) could not be added to a shelf or My Panel. They can now; a shelf copy of Remove Hidden does the same as the button, undo included. A UI test (`shelf_every_control`) now opens every palette and fails on any control that cannot be added, so a new one cannot slip through; the few left out on purpose (lists, text boxes, presets, menu rows) are listed with their reasons.
fixed: Holding **Alt** and clicking a text box in a palette (for example Tool > Selection Sets > Set name) closed the app when Alt was let go; the box now just closes.
testers:
  - Any OS: Tool > Visibility, hide part of a mesh, Alt+drag **Remove Hidden** onto a shelf (or hold it still half a second and pick a shelf); click the shelf's copy, the hidden part goes, Ctrl+Z brings it back.
  - Tablet or pen: the same with a long press on **Delete masked**, **Fill** (Color) and **Solo** (Tool > Parts).
---
Branch fix/shelf-draggable-all. Cause: a control joins the shelf / My Panel entries only when ui::HelpText / HelpAction follows it; the
raw ImGui buttons had a tooltip but neither. Remove Hidden and Show all now use their registered actions (ui::ActionButton); the rest call
the new ui::PinControl. audit_pins (UiTest.cpp, Ui.cpp g_audit) is the walk; tests/ui/shelf_every_control.excluded is the exclusion list.
