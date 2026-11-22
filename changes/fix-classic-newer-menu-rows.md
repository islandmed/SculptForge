---
whats_new: **Classic menus keep their old rows**: File > Render with..., Preferences > Scripts and > Render bridge show in the SculptForge layout; in Classic, turn them on with **Preferences > Interface > Show newer menu rows**.
changed: File > Render with... (preview), Preferences > Scripts and Preferences > Render bridge (added after 0.13.0) are hidden in the Classic
  layout until Preferences > Interface > Show newer menu rows is on, or until you set the feature up (Python scripts enabled, a render
  helper chosen). They stay on in the SculptForge layout. The commands and the command search are unchanged.
testers:
  - Classic layout, fresh preferences: File has no "Render with..." row and Preferences has no Scripts or Render bridge rows.
  - Preferences > Interface > Show newer menu rows: the three rows appear; in the SculptForge layout they are there by default.
---
Audit row 8 (~/Work/.sf/state/classic-drift). The setting is unset by default (follows the layout) and stored only once changed.
