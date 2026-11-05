---
whats_new: **Pen pressure per brush**: **Brush > Pen pressure** sets Size and Strength to **Use global**, **On** or
  **Off** for the brush you hold (also in Brush Lab and **Edit > Preferences > Tablet**). Grab brushes start with
  Size off.
added: **Pen pressure per brush** (#122, requested by Zoldyck-Kill): every brush has its own **Pen pressure: Size**
  and **Strength** switches, **Use global** / **On** / **Off**, in **Brush > Pen pressure**, Brush Lab and
  **Edit > Preferences > Tablet**. **Use global** follows Pressure > Size / Strength as before; **On** and **Off**
  override them for that brush. Kept per brush with its other settings (**Save Brush Settings** keeps them, **Reset
  Brush to Default** puts back the default) and in a custom brush's `.sfbrush`. Move, KelvinGrab, KelvinTwist,
  KelvinScale, Tendril and Elastic Tendril start with Size off; older saved settings load as Use global. Command:
  `brush_pen_pressure size=on|off|global strength=on|off|global`.
testers:
  - Windows + pen tablet: turn on Pressure > Size on the shelf. Clay: a light stroke is thin. Brush > Pen pressure >
    Size > Off: the same light stroke is full width. Pick Clay again: still thin (Use global).
  - Pick Move: Brush > Pen pressure shows Size: Off; a light press grabs with the whole brush circle.
  - Set Standard's Strength to Off, Save Brush Settings, restart: Standard still draws at full strength with any pressure.
---
