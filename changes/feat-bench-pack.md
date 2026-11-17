---
whats_new: **Benchmark scene pack**: download a pack of our own sculpts and run `sculptforge --benchmark-pack bench-pack --out results.json` on any computer; the result lists your hardware and the frame times, so computers can be compared.
added: **Benchmark scene pack** (`sculptforge --benchmark-pack DIR --out FILE`): the scenes in a downloadable pack
  (`bench/pack`, our own Creature Head divided to 5.8, 23 and 92 million triangles, the first of them the entry-level
  "floor" test) get the standard strokes, and one JSON result records the hardware (CPU, GPU, driver, RAM, OS, hardware
  tier), the SculptForge version, the pack's hash and, per scene and stroke, frame time p50/p95/p99, frames a second,
  stroke lag and the slowest frame. Scenes the computer has no memory for are left out and said so. The
  benchmarks page lists community results, sortable by hardware, sent in through a "Benchmark pack result" issue.
testers:
  - Any OS: unpack the pack from sculptforge.app/benchmarks, run `sculptforge --benchmark-pack <folder> --out results.json`; about 5 minutes, exit code 0, results.json written.
  - A computer with 8 GB of memory: the 92 million triangle scene is skipped with a message, the other two are measured.
---
Reviewers: the site part (website/benchmarks) is its own branch and goes live with the release that has this command.
