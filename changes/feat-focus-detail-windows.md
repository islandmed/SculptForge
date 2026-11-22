---
added: Windows: Focus Detail streams very dense Micro levels (past about 300 million triangles) from a .sfdetail file beside the project, as on Linux and macOS: full detail loads where you look and sculpt, saves write only what changed.
fixed: Focus Detail files saved on Linux open on Windows and macOS too (every platform reads every detail file).
---
Reviewers: src/core/FocusDetail.cpp (Windows file layer: mapping, positional writes, rename over a mapped file, write codec), third_party/zstd (decoder only, v1.5.7, README has the SHA-256 and how to update), tests/test_focus_detail.cpp (three new tests), tests/data/focus_detail_linux.sfdetail (fixture written on Linux).
