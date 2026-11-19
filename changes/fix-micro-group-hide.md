---
fixed: **Hide and Isolate face group on micro detail**: H (hide the face group under the cursor), Shift+H (isolate it)
  and Tool > Visibility now hide the micro detail of those triangles too (before, a model with a micro level kept
  drawing everything, so nothing seemed to happen). Show All and Ctrl+Z bring it back.
testers:
  - Any OS: add a micro level (Tool > Geometry > Add Micro) to a model with face groups, hover a group, press Shift+H:
    only that group stays; Shift+H again (or Show All) shows everything. H hides just the group under the cursor.
---
