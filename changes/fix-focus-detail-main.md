---
fixed: Focus Detail (experimental, `--focus-detail`): the app loop now loads full detail where the camera looks (it
  only did in UI tests); a stroke that widened the displacement range left the tiles not yet loaded at the old scale
  (they now follow, and Save writes them at the new scale).
testers:
  - Linux or macOS: `sculptforge --focus-detail head.sfdetail` (make one with `sculptforge-cli --bench-focus`), zoom
    into the face: pores appear within a second. A strong Inflate stroke, then orbit: no tile looks flattened or
    spiky, and after Save + reopen it looks the same.
---
