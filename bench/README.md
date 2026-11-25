# bench

Benchmark scenes, results and harnesses. Performance is a feature here: every number we publish comes from a
benchmark you can run yourself, and the first machine a change has to hold on is the weakest one we support (the
"floor" tier: an integrated GPU or a base laptop), then the bigger ones. How the numbers are made and quoted:
[docs/benchmark.md](../docs/benchmark.md) and [docs/performance.md](../docs/performance.md).

| Folder | What |
|---|---|
| [pack](pack/README.md) | The public **bench pack**: scenes and a fixed set of pen strokes you run on your own computer (`sculptforge --benchmark-pack`) and compare with everyone else's. It writes one JSON file of frame-time percentiles, lag behind the pen and the slowest frame. `hashes.json` pins the files |
| `floor/` | Recorded results (`sculptforge-benchmark/1` JSON) from an Apple M1 Pro laptop, with the floor-tier settings on and off and on its efficiency and performance cores, named `<machine>-<commit>-<scene>-<setting>.json`. They back claims such as "the floor tier does not regress" |
| `windows/` | The same kind of result from a Windows PC with a discrete GPU |
| [mcp-detail](mcp-detail/README.md) | How far an AI agent driving SculptForge over MCP gets towards fine detail: one fixed prompt, judged by pictures and by geometry numbers |

## Running the standard benchmark

```sh
sculptforge --benchmark standard-15m          # the standard scene: a procedural head of about 15 million triangles
sculptforge-cli --bench --bench-sizes 4,16    # the CPU side only, no window
```

The standard scene is built from a formula, not a file, so every platform sculpts the same surface. A result is
valid only for the build that made it: it records the commit, the hardware and the settings.

## Rules for results

- **Measure real quantities** (frame time, memory, stall length) with the benchmark's own harness. Report the slowest
  frame, not only the average, and peak RAM beside it: a speed-up that leaks memory is not one.
- **Compare like with like:** same scene, same stroke set, same machine, and rerun when the noise (the spread
  between runs) is bigger than the change you claim.
- Do not run a benchmark while something else is using the machine, and do not quote another program's numbers.
- Add a result file here only when it backs a claim that is published or protects a gate; name it as above.
