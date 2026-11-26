---
fixed: **Group visible / Ctrl+W**: after hiding part of an ungrouped mesh, Group from Mask with nothing masked now makes the visible part its own face group. It kept group 0, the same group the hidden faces have, so nothing changed. A fully visible mesh and an already grouped mesh behave as before.
---
Reviewers: src/core/FaceGroups.cpp (groupsMergeVisible), tests/test_facegroups.cpp, tests/ui/facegroup_visible_fresh_id.sfui; tooltip, shortcuts.md, face-groups.md and the Japanese string follow the new wording.
