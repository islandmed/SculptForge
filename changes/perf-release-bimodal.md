---
changed: `SF_STROKE_LOG=1` (start SculptForge with it set) now gives each stroke batch, commit and stroke end the time it
  happened (and on Linux the CPU core it ran on), and marks the batches that run after the pen lifts (catch-up, finalize). On Linux with a CPU that has
  efficiency cores, a slow release then shows whether its last batches ran on one.
testers:
  - Any OS: start SculptForge with `SF_STROKE_LOG=1`, make a long stroke on a dense mesh and lift the pen; the log names
    the catch-up and finalize batches with their times. Without the variable nothing is logged and strokes feel as before.
---
The benchmark's release row now reports the release's end work (finalize batch, upload packing, stroke end) apart from the
catch-up dab that was still running when the pen lifted. The catch-up is what made the release time bimodal; the total stays
as an info row.
