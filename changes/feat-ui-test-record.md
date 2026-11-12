---
added: **`--ui-test x.sfui --record out.mp4`**: a UI test script also writes every frame to a small mp4 (crf 23, `--fps` and
  `--size` honoured; needs ffmpeg), for before and after clips of a bug. `--ui-record` keeps the real-time launch-clip mode.
---
Tooling: bug-fix branches ship before.mp4 + after.mp4 made this way (~/Work/.sf/areas/qa-plan.md).
