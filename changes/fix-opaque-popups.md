---
whats_new: **Opaque popups**: menus, popups, tooltips and windows no longer let the interface behind them show
  through (it was easy to see at 200% scale), in every look and colour scheme.
fixed: Popups, menus, tooltips, the Command Palette, the Quick Menu and floating windows are fully opaque in every look
  (Classic, Forge Terminal and its schemes, Phosphor, Blueprint); at 200% the Preferences popup showed the Sculpt
  Timeline's "Save with project" and the shelf text through it. Overlays on the 3D view keep their translucency.
testers:
  - Windows at 200% scaling: Edit > Preferences with the Sculpt Timeline showing; nothing behind the popup reads through.
  - Any OS: switch Preferences > Interface > Look through each look; open a menu and hover for a tooltip, both solid.
---
UI test: tests/ui/opaque_popups.sfui (style_popup_alpha / style_menu_alpha / style_window_alpha == 1 per look and scheme).
