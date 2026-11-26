---
whats_new: **Paint > Layers**: a compact list of the paint layers above the material channels, with each layer's eye, blend mode and opacity, so painting stays in one palette.
added: **Paint > Layers** (the Color palette in the Classic layout): the Part's paint (or texture) layers in a compact list, above the material channels: select a layer, show or hide it, set its blend mode and opacity, add a layer. It is the same stack as Tool > Paint Layers (same data, order and undo). Opens and closes, remembered; in the Classic layout it appears with Show newer menu rows.
member_request: true
testers:
  - Paint menu: open Layers, add two layers, select one, hide it with its eye, set Multiply and lower the opacity; Tool > Paint Layers shows the same layer, mode and opacity. Ctrl+Z steps back each change.
  - Narrow tray (1440 px wide window): a long layer name is shortened with "...", the row still shows its eye.
---
Reviewers: src/app/PanelPaintLayers.cpp (drawColorLayersCompact; the blend/opacity editor is now shared as drawActiveLayerBlendOpacity), PanelVertexPaint.cpp (the section), UiTest.cpp (three values), tests/ui/color_panel_layers*.sfui.
