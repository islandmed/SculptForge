---
whats_new: **Controller buttons**: any button of a game controller (or SpaceMouse) can run any action. In **Edit > Preferences > Hotkeys** click the controller button beside an action and press the button; a pie menu, the Quick Menu or the Stroke popup on a button works in Clean view too.
highlight: Game controller buttons as shortcuts
added: **Controller buttons** (Edit > Preferences > Hotkeys, Input > Controller): a game controller's buttons and triggers, and the SpaceMouse's buttons, bind to any action the way keys do (click the controller button beside the action, press the button; Esc cancels, Backspace clears). The action runs on the press; a pie menu, the Quick Menu and the Stroke popup follow the button as they follow a key (hold, point, let go; or tap and click), in Clean view as well. Plug and unplug while running; buttons only, no sticks or rumble; nothing changes without a controller.
member_request: true
testers:
  - Any OS with a game controller: Edit > Preferences > Hotkeys, filter "Radial menu 1", click the controller button beside it, press a controller button. Hold that button over the model, point with the mouse or pen, let go: the item runs.
  - Same in Clean view (Shift+F11). Unplug the controller while holding the button: the pie closes.
  - A SpaceMouse owner: bind one of its buttons the same way (it beats the setting in Preferences > Input > SpaceMouse).
---
Reviewer: input path. Design: the binding table is core/Controller.h (ui.json "controller"); src/app/Controller.cpp opens gamepads
through SDL's gamepad API and runs the action with a pseudo key code so the pie menu / Quick Menu / Stroke popup hold logic is the
one a key uses. The SpaceMouse's own button list stays for unbound buttons. Local input only: no network, no new ports.
