---
added: "The cursor on a very dense Micro level (the base mesh much coarser than the detail) tests only the part of each triangle's detail grid the ray can reach, so hovering and picking cost far less per frame there; the surface found is the same as before."
testers:
  - "Any OS: a very dense Micro level (about 300 million triangles or more, Focus Detail streaming): move the cursor over the surface and orbit; the cursor sits on the surface where it did, and nothing flickers. Mouse, pen and trackpad."
---
Reviewers: src/core/Micro.cpp (microRaycast window), tests/test_micro.cpp (micro_raycast_window_matches_every_triangle: the windowed pick equals testing every triangle). Input rule: full T0 with SF_READY_BASE.
