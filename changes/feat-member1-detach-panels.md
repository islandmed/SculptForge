---
whats_new: **Panels in their own windows**: every palette has a new **Open in its own window** icon in its header, and
  the **User Guide** and **Controls** have one in their toolbar. Drag that window to your second monitor or pen
  display; it opens there again next time. Close a palette's window (or **Back in the main window**) to bring it back;
  **Preferences > Config > Reset panel windows** brings them all back. Requested by Richard Throgmorton (MakIt3D), our
  first Founding Member.
highlight: Palettes, the User Guide and Controls on your second monitor
added: **Panels in their own windows** (multi-monitor): any palette, the User Guide and Controls open in a window of
  their own from the new icon in their header or toolbar, to keep on another monitor or a pen display. Where each
  window was is remembered between sessions (per computer, panels.json, not in shared interface files); a monitor that
  has gone brings the panel back beside the main window. Shortcuts keep working while a panel window has the keyboard.
  **Reset panel windows** (Preferences > Config, command palette; also part of Reset Layout and Shortcuts) puts every
  panel back. On Wayland the compositor places the windows (their size is remembered, not their position). Requested
  by Richard Throgmorton (MakIt3D), Founding Member #1's Member Request.
testers:
  - Windows, two monitors: Tool palette header > Open in its own window; drag it to the second monitor; sculpt a few
    strokes, change Draw Size in the panel; quit and start again: the panel is back on the second monitor.
  - Windows at 150% on one monitor: the panel window's text is sharp and the same size as the main window's.
  - F1 (Controls) > Open in its own window; close it; press F1 again: it opens on the monitor it was on. Press B with
    the panel focused: the brush picker opens (shortcuts work from a panel).
  - Unplug the second monitor (or change the layout), start: the panel opens beside the main window, not off screen.
  - Linux Wayland (Hyprland, GNOME): a palette window opens and works; its size is kept (position: the compositor's).
  - macOS: the panel window follows the main window into full screen Spaces sensibly; Cmd+Q quits with it open.
  - Preferences > Config > Reset panel windows: every panel is back in the main window.
---
Design: docs/design/detach-panels.md (one OS window + ImGui context per panel; no docking branch, no coordinate change).
