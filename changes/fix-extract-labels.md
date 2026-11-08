---
whats_new: **Extract labels in full**: **Tool > Parts > Extract** reads **Thickness**, **Edge smooth**, **Both Sides**,
  **Corners** and **Border**, and in a narrow tray (a small window, 150% or 200%) the controls move onto their own rows instead of cutting words off.
fixed: **Tool > Parts > Extract** and **Project** labels were cut off in a narrow tray (since 0.11.11). The settings now
  read **Thickness**, **Edge smooth**, **Both Sides**, **Corners** and **Border**; a pair shares a row only when both labels fit, else each takes its
  own, and Both Sides / Corners / Border wrap. Scripts use `triangle_corners` / `triangle_border`; the old argument and
  action names still work.
testers:
  - Windows at 150% and 200% scaling, a 1024-wide window: Tool > Parts; every Extract and Project label reads in full.
  - Any OS: mask the top of a sphere, Extract, toggle Both Sides, Corners and Border; the preview follows each click.
---
UI test extract_labels (also at 1024x768 and 1440x900) fails if any Extract/Project control's label is wider than the
control (new clipped_labels@SCOPE value, reported by ActionButton, SliderInside and ui::Checkbox).
