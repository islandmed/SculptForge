---
changed: Scripts, commands and file names use SculptForge's own names throughout (`part_*`, `forgesphere_*`,
  `bridge_*`, `facegroup_*`, `load_litsphere`, `library_open`, `layout=classic`, `input_style classic`, the
  `--facegroups` flag, the `SCULPTFORGE_LITSPHERE_PATH` variable). Scripts, macros, keymaps, radial menus, packs,
  projects and preferences from older versions still load: their old names are read once and mapped to the new ones.
  The old command-line flag and environment variable names are no longer read.
removed: The library export plug-in for another sculpting app and opening its library files through it. A folder of
  numbered Bridge files still opens as one scene.
testers:
  - Any OS: open a project saved with 0.10.x; Parts, face groups, vertex paint, the Lit Sphere and the Forge Spheres come back.
  - Any OS: run a macro or script recorded with 0.10.x (it may use the old command names); it runs as before.
  - Windows/macOS: File > Bridge > Send, then send back from the other app; File > Bridge > Receive updates the Parts.
---
fix/ip-purge-2: no third-party marks anywhere in the tree (docs/legal/trademarks.md is the only file that names them).
