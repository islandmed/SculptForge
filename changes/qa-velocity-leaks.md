---
changed: **Tests** (contributors): `check_marks.py` skips files whose exact content already passed with the same marks
  list (36 s to under a second when warm, never on CI); the budget check's rerun-alone limit grows with the machine's
  load (at most 3x, logged); main's T0 workflow builds nothing for a site-only push.
---
