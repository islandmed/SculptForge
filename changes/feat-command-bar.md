---
whats_new: **Say what you want** (<kbd>F3</kbd>): type `clay 40`, `intensity 30`, `mirror x`, `remesh 200k` or `hide all but head` in the search and the first row shows what <kbd>Enter</kbd> will do. Typos are forgiven, every interface language works, <kbd>Up</kbd> brings back your last commands, and the result line shows the key.
highlight: Say what you want in the F3 search
added: **Say what you want** (F3 search): plain words with numbers run existing actions. `clay 40` (brush and size, `30%` for intensity), `intensity 30`, `size 40`, `mirror x`, `symmetry off`, `wireframe on`, `remesh 200k` (about 200 000 triangles) or `remesh 300` (voxels), `hide all but head`, `show all`, `isolate body`. Typo-tolerant, matches the names in every shipped language and Part and custom brush names, shows "Clay - Size 40" before Enter and the action's shortcut in the result line afterwards. The last 50 commands are kept (Up arrow). Offline, no scripting: unknown input stays a normal search.
testers:
  - Windows, Spanish interface: press F3, type "intensidad 20" and "simetria x"; the first row says what Enter does.
  - Any OS: F3, "clay 40", Enter; the Clay brush is active at size 40 and the result line shows its key. F3, Up arrow, Enter runs it again.
  - A scene with Parts named Head and Body: F3, "hide all but head"; only Head stays visible. "show all" brings the rest back.
---
The parser is core/CommandParse.* (no UI, no App); src/app/CommandBar.cpp builds the catalog and runs a reading through the
existing actions and commands. Tests: tests/test_command_parse.cpp, tests/ui/command_bar.sfui.
