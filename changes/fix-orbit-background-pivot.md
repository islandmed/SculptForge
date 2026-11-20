---
whats_new: "**Orbit beside a small model**: pressing on the background next to a small, off-centre model no longer swings it off the screen. With **Orbit around the pointer** on, the view turns about where you last sculpted, or about the model."
fixed: "**Orbit around the pointer**: a rotation started on the background while the middle of the view was on empty space (a small model panned aside) turned about that empty middle and swung the model away. It now turns about the last stroke if that is in view, else the middle of the model as it shows. Over the model, and with the preference off, nothing changes."
added: "**Frame last stroke** (no key by default: bind one in Preferences > Hotkeys): moves in on where you last sculpted and keeps the view's angle."
testers:
  - "Any OS: Preferences > Input > Orbit around the pointer on. Pan a small model off to one side, press on the background just beside it and drag: the model turns in place and stays on screen."
  - "Sculpt a stroke, press Frame, then bind a key to Frame last stroke (Preferences > Hotkeys) and press it: the view moves in on the stroke."
---
Same branch as the orbit fix: the reporter also wants a way back to where they last sculpted after Frame shows everything.
