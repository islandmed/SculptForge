---
added: "The cursor on a very dense Micro level (the base mesh much coarser than the detail) tests only the part of each triangle's detail grid the ray can reach: about 17% less pick time on the billion-triangle scene (picking was over half of the main thread there); the surface found is the same, point for point."
testers:
  - "Any OS: a very dense Micro level (about 300 million triangles or more, Focus Detail streaming): move the cursor over the surface and orbit; the cursor sits on the surface where it did, and nothing flickers. Mouse, pen and trackpad."
---
Reviewers: src/core/Micro.cpp (microRaycast window), tests/test_micro.cpp (micro_raycast_window_matches_every_triangle: the windowed pick equals testing every triangle). Measured on the floor tier, standard-1b main thread (5 ms samples): microRaycast 58.2% of 59.3 s -> 51.1% of 56.0 s (34.5 s -> 28.6 s); bench.sh quick: no row worse; artist-qa pass on mouse and pen (556 values identical to main). Input rule: full T0.
