---
whats_new: **Version at a glance**: the window title and the top bar show the SculptForge version, so a screenshot
  says which one you run. A beta's tag shows in the accent colour.
changed: The window title starts with the project name and a "*" for unsaved changes (taskbars stay readable), then
  SculptForge and its version, and the top bar shows the version, small and dim, beside the name in both layouts (a narrow window drops it).
  A narrow window or a high UI scale (a 1024 px window at 200 %) never draws top bar items over each other any more:
  the version goes first, then the layout presets and Save Snapshot, the search field shrinks to its icon, and the
  menus that don't fit open from a >> button.
  Betas show their tag ("0.12.0 beta.2") in the accent colour; builds from a git checkout show "dev" and their commit.
testers:
  - Any OS: the window title and the taskbar entry start with "Untitled", then SculptForge and the
    version; sculpt a stroke, a "*" follows the name.
  - Windows at 150% and 200% scaling, both layouts: the version sits beside "SculptForge" in the top bar and never
    overlaps the menus or the project name; shrink the window, it disappears before anything crowds.
---
Version source: CMake's PROJECT_VERSION + SCULPTFORGE_PRERELEASE; dev commit from SCULPTFORGE_DEV_BUILD (AUTO: git
checkout, not CI). UI test tests/ui/title_version.sfui (+1440, 1024).
