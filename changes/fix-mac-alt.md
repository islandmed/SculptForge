---
changed: **Keyboard and mouse** guide: Option is Alt on macOS, and how to make Option Alt and Cmd Ctrl when the
  Windows version runs under CrossOver or Wine on a Mac (their Mac driver sends Cmd as Alt by default; thanks
  @fenixpoints).
testers:
  - macOS (native build): Option+click a Part selects it; Option+drag in the viewport pans; Cmd+S saves. Cmd never acts as Alt.
---
Reported as a 0.11.16 regression; the reports were all the Windows build under CrossOver. Nothing in v0.11.12..v0.11.16
changes modifier mapping (chordFromEvent, ctrlDown, Input/App modifier code, SDL pin 3.2.30 unchanged). Cmd as Alt is
Wine's Mac driver default (Command => Alt, Option unbound). Test action_mac_modifiers pins the native mapping.
