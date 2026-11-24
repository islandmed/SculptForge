---
fixed: "**Fill layer sliders jumping back**: letting go of a Mask effect slider (Noise Size and Contrast, Fill Value, Levels, Blur, the generators) or a Fill layer's Roughness, Metallic or Height slider put the old value back, so the effect never changed. The dragged value now stays, and Undo and Redo move between the old and new values. The same fix covers the Opacity slider in a layer channel's right-click menu."
testers:
  - "Any OS: Paint Layers > Fill Layer > Mask > Add mask effect > Noise: drag Size and Contrast and let go; the numbers and the surface keep the new values. Ctrl+Z returns the old value, Ctrl+Shift+Z the new one."
  - "Any OS: same for the Fill effect's Value, Levels, Blur, and the layer's Roughness (turn R on first), Metallic and Height; and right-click a layer's R chip: drag Opacity and let go, it stays."
---
Reviewers: new ui::SliderOnRelease (Ui.h/Ui.cpp): the old pattern `float v = stored; SliderFloat(&v); if (IsItemDeactivatedAfterEdit()) run(v)` committed the stored copy because the release frame has no drag update. Used by PanelFill (material + effect sliders) and PanelPbr (chip opacity). Test: tests/ui/fill_effect_slider_keeps.sfui (+ _1440/_2880); fails without the helper's held value.
