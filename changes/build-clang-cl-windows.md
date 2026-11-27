---
changed: **Windows builds** are made with clang-cl 20.1.8 instead of MSVC. The heavy brush and mesh loops run faster on
  the same hardware; strokes produce the same results (no fused multiply-adds, no fast-math), and the installer, zip
  and signing steps are unchanged.
testers:
  - Windows: install the zip or installer, sculpt a few strokes with Clay, Smooth and Inflate, save and reopen a project.
---
The compiler version is pinned by an assertion step in ci.yml and packages.yml; a different clang-cl fails the build on purpose.
