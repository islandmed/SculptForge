---
added: Shortcuts ask before they take a key. Press a key that another action uses and a Replace / Cancel box names that action (Enter replaces, Esc cancels); before, the key moved silently. Requested by Artizmo on Discord.
added: A changed shortcut has a reset arrow beside it in Preferences > Hotkeys that puts its default back (and takes that default from whoever holds it now).
added: Pan left, Pan right, Pan up and Pan down (Ctrl+Numpad 4, 6, 8, 2 by default, in every key style, bindable in Hotkeys). A press steps; holding the key keeps moving, and so do Zoom in and Zoom out. Preferences > Input > Pan and zoom key speed sets how fast.
fixed: Ctrl+Alt+click on a shelf button no longer starts a shortcut change when the pointer moved (a customize drag rebound keys such as F). Reported by Fenix on Discord.
---
Conflict detection compares whole chords: Ctrl+Z and Z are different keys. Multi-key sequences ("A then S") are not built; the binding model still has room for them.
