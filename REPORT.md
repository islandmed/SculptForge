# fix/size-keys

**What:** askNK's email (10-31) says the brush size and intensity keys (S / U) "do not currently seem functional".
They tested a build from before 0.9.6.

**Result: not a bug on main.** The new UI test `tests/ui/size_keys.sfui` presses S and U, drags, and checks that Draw Size
and Z Intensity change. It covers the SculptForge (default) and ZBrush input styles, and also runs after a panel click.
It PASSES on origin/main 5d2d886 (149 frames). askNK most likely hit #9 (the S/U slider closed as soon as you grabbed it,
on Windows) and/or #13 (the Windows Ink slider couldn't be dragged). Both were fixed in 0.9.5.

**Changes:** this test only. No code change and no CHANGELOG line, since nothing was fixed.
**Tests:** `sculptforge --ui-test tests/ui/size_keys.sfui` PASS (Linux, M1 Pro). It is picked up by the ui test glob,
so ctest runs it as `ui_size_keys`.
**SHA:** 9fed628 (branch fix/size-keys). Merge is optional: it's a regression test only.
