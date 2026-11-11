---
whats_new: **Projects with no models** open: a project saved without any Parts loads as an empty scene, with its settings, lights, cameras and views.
fixed: A project whose Part list is empty no longer fails with "Project has no valid Part list"; it opens as an empty scene (settings, lights, cameras, views and shelves load) and saves back the same.
fixed: The guide's 3D printing section pointed to File > Bridge > Export STL; Export STL is under **File > Export**.
testers:
  - Any OS: open a project that has no models (saved by a script or another tool); it opens empty, with its lights and views.
---
Loader: src/core/Project.cpp (empty list becomes one empty "Sculpt" Part, the Document invariant). Test: project_empty_part_list_loads_and_round_trips.
