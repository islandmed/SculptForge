---
changed: **Maya Live link** (#42): on very large meshes the mid-stroke updates to Maya come less often (at most about
  4% of the app's time), so sculpting stays smooth; the link is now tested on Windows' named pipe as well.
testers:
  - NOT YET VERIFIED in a real Maya (tests use a mocked maya.cmds; Windows is verified: the named-pipe link with the
    shelf's own client in CI, Live's auto-send and the link's listener on a desktop RTX 3060): report anything odd on #42.
---
Follow-up to 0.11.11's Maya Live (feat/maya-live 55ebc644..460bbcec, mayalive). Windows verified 11-06 (CI
42344073618; the 3060 desktop: ui maya_live passes on the release build). Bench (laptop, load < 2): mid-stroke diff
16 ms at 28M triangles, so the streaming interval is 25x its cost (at least 100 ms).
