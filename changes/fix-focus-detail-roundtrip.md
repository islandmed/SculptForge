---
whats_new: **Focus Detail**: opening a detail file shows its fine detail (pores, scratches) at once, `open` in a script takes .sfdetail files, and **Open** lists them.
fixed: **Focus Detail**: a scene opened from a `.sfdetail` file, or reopened from its project, drew its fine shading detail (pores) only after the next stroke or edit when nothing else kept the viewport drawing; the viewport now keeps drawing until the detail's shading is in. The detail itself was never lost: the project's `.sfdetail` copy holds every tile (tested byte for byte).
fixed: **Focus Detail**: the script command `open` takes a `.sfdetail` file (it said "No faces found"), and File > Open lists `.sfdetail` among the files it shows.
fixed: **Save As** into a folder that doesn't exist yet makes the folder (a project with a streamed detail file failed with "Can't write ... .sfdetail.part"); a folder that can't be made says so.
testers:
  - Linux or macOS: open a .sfdetail (File > Open, or drag it in); zoom to the surface without sculpting; pores and scratches are visible within a second.
  - Save it as a project in a folder you type that does not exist yet; reopen the project: same look.
---
Reviewers: render/MicroFocus.* (levelsChanged_, unpaged_), Renderer/MicroRenderer pendingPages, App::wantsFrames, App::runCommand ("open" of .sfdetail), App::openFocusDetail now returns bool, Project.cpp saveProject (folder). FocusDetail.cpp/.h untouched.
