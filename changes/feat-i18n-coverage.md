---
whats_new: **Spanish and Traditional Chinese, complete**: every menu, panel, tooltip and Ctrl-help text is now translated in both languages (UV Editor, Quick Remesh, Preferences > Input, Packs, Bake and the rest).
changed: **Spanish and Traditional Chinese cover the whole interface**: about 950 labels and help texts added to each (UV Editor, Quick Remesh, Preferences > Input, Packs, Bake, Fill layers, Forge Spheres, SpaceMouse...), from 47% and 44% of the interface to 100%. A new test keeps it there: a label without both translations fails the build.
testers:
  - Any OS: Preferences > Interface > Language > Español, then 繁體中文: open Preferences > Input, the Help menu, Tool > Remesh and the UV Editor (Alt+U); no English left, nothing cut off, no "?" boxes.
---
tools/i18n/coverage.py lists every string the code hands to tr() (and the wrappers that do) and what each table lacks;
ctest i18n_coverage runs it with --check. Names that stay English: tools/i18n/keep_english.txt.
