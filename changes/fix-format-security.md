---
fixed: The Arch package builds with strict compiler flags (`-Werror=format-security`, as CachyOS's makepkg sets
  them): text and tooltips are drawn as plain text, never read as printf formats, so a translation or name holding
  `%` can't garble a label or crash (thanks @itotochi1107, #162). Every Linux and macOS build now fails on a
  non-literal format, so it stays fixed.
security: Interface text, translations and tooltips are never interpreted as printf formats (#162).
testers:
  - Arch or CachyOS: `makepkg -si` in packaging/arch with the distro's default makepkg.conf builds without errors.
  - Any OS, Spanish and Chinese: Help > Welcome, the gizmo help (W) and Preferences > Performance show "10%" and
    "75%" with one percent sign, and tooltips read as before.
---
Reviewers: 93 call sites in src/app (ImGui Text*/BulletText/SetTooltip with a tr() string and no arguments) now pass
"%s"; PanelBrushSets.cpp's format() template has an else branch so snprintf is never instantiated without arguments.
The flags are set in CMakeLists.txt (add_compile_options, GCC/Clang), so CI, T0, packages and any new Linux job get them.
