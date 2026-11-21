---
fixed: **Voxel Remesh and other memory checks on a Mac under Rosetta**: the free memory was counted in 4 KiB pages instead of the host's 16 KiB, so it read four times too low and a modest Voxel Remesh was refused as "needs swap". The Python package's macOS test no longer waits for ever when a remesh is refused.
---
Found by the macOS wheel test hanging in 0.13.10 (a worker thread died on the refusal and the test loop waited on it).
