---
added: Focus Detail in projects: **Divide to Micro** past 300 million micro triangles streams the new level from a
  detail file (`.sfdetail`) instead of holding it all in memory; full detail loads where you look and sculpt. Save
  writes `<project>.sfdetail` beside the `.sfproj` (only the changed tiles, compacted once half the file is
  garbage) and opening the project streams from it again. Project format 9 (only for projects with a streamed level).
  Linux and macOS for now: Windows keeps such levels in memory, and a project with a streamed level opens on Linux
  and macOS only until Windows streaming lands.
fixed: Focus Detail: saving a project with a streamed level kept only the coarse detail; bake, displacement export,
  surface noise and copies of a streamed level now use full detail.
testers:
  - Linux or macOS, 16 GB: a head, Divide to Micro to about 1 billion (Tool > Geometry > Micro); the Micro panel says
    "Streamed". Sculpt Clay, Move and Smooth up close, Ctrl+S, quit, reopen: the detail is all there.
  - Save As to another folder: a second .sfdetail appears beside the new project; the old project still opens as
    it was saved.
  - Windows: open a project with a streamed level saved on Linux or macOS (its .sfproj and .sfdetail): a clear
    message that it needs Linux or macOS for now, nothing else breaks (the app stays up, other projects open).
---
Phase 3 of the billion-triangle brief (core/FocusDetail.h create/attach/saveAs/rescale/ensureAllFull, Project.cpp
format 9, MicroCommands Divide). No whats_new line until the bench numbers are in (the guide page waits for them).
Windows keeps the level in memory until phase 4 (CreateFileMapping).
