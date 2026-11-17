# SculptForge bench pack

A public benchmark you can run on your own computer and compare with everyone else's.

```
sculptforge --benchmark-pack bench-pack --out results.json
```

`bench-pack` is this folder, unpacked. The command opens SculptForge with temporary settings (your own settings and files are not touched), builds each scene of `pack.json` in turn, plays the same pen strokes on each (Clay at 25, 60 and 120 px,
Move, Smooth, a 240 px brush, then an orbit), and writes one JSON file with your computer's hardware, the SculptForge
version and, for every scene and stroke, the frame time percentiles (p50, p95, p99), frames a second, the stroke's lag
behind the pen and the slowest frame. About 5 minutes. Close heavy programs first.

The scenes are one sculpt, `creature-head.sfproj`, divided 5, 6 and 7 times: 5.8, 23 and 92 million triangles.
`creature-floor` is the **floor tier** test: the entry-level size every supported computer should sculpt on. A scene
that needs more free memory than the computer has is skipped and said so in the result (that is a normal result on a
small machine); a damaged pack is refused.

Send the result to be listed at <https://sculptforge.app/benchmarks/>: open a *Benchmark pack result* issue and paste
the JSON. The format and how results are checked: `docs/benchmark.md` in the SculptForge repository.

## Contents and licence

`pack.json` (the manifest: scenes, how often each is divided, the triangle count it must come out at) and
`creature-head.sfproj`, the Creature Head starter mesh, made by SculptForge's own scripts (`tools/starter`). All of it
is released under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/): no rights reserved, use it for anything.
`python3 tools/benchmark/make_pack.py` builds this folder and its zip from the repository, and prints the pack's SHA-256
(of `pack.json`, then each scene's file), which every result carries as `settings.pack.sha256`.
