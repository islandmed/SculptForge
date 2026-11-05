---
whats_new: **Traditional Chinese**: **Preferences > Interface > Language** has Traditional Chinese: it puts the menus, palettes,
  shelves and messages in Traditional Chinese (thanks yoyomaimai). Brush and tool names stay in English.
added: **Traditional Chinese interface** (#114, translation by yoyomaimai): Preferences > Interface > Language
  (the button with its Chinese name; saved as `zh-TW`). About 740 labels, Taiwan usage, brush and tool names kept in English; anything not
  translated yet stays in English. The Chinese glyphs come from a small embedded subset of Noto Sans CJK TC
  (SIL OFL), loaded only while the interface is in Chinese.
testers:
  - Any OS: Preferences > Interface > Language > Traditional Chinese: the palette bar, Preferences and Tool > Geometry read in
    Chinese with no "?" boxes, at 100% and 200% UI scale and in a non-Classic look. Back to English: all English.
  - Any OS: restart with Chinese chosen: it starts in Chinese.
---
Reviewed every string of the contributed table: 35 stale entries dropped (old labels), 3 printf-order fixes,
term consistency (部件, 資源庫, 工具列, 工具匣), ~60 hand fixes in the last groups. Product names kept English.
