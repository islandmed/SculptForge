---
fixed: Monochrome: the Command palette's frame and highlighted row, the shortcut boxes, the radial menu's highlight, the corner gizmo's hover dot, the guide's search highlight and selected text in a field were still orange. They take the white accent now, and the Library's selected page button keeps a light fill when hovered so its dark text reads.
testers:
  - Edit > Preferences > Interface > Look > Colours > Monochrome, then F3: the palette's frame, the highlighted result and its icon are grey and white, with no orange.
  - Right-click a brush, press T for the radial menu, select text in a field, hover the corner gizmo's dots: no orange anywhere in the chrome.
---
Reviewers: src/app/Theme.h (ink() for colours too), src/app/CommandPalette.cpp, tests/ui/mono_popups.sfui (tint@ probes over the palette, a context menu and the radial menu; the Neutral colours keep their orange).
