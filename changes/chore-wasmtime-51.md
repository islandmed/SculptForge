---
whats_new: **Plugin sandbox updated**: plugins now run on wasmtime 51.0.0. Nothing changes for plugins or their limits.
security: Plugin sandbox runtime updated: wasmtime 50.0.2 to 51.0.0 (#235), which includes the fix for an upstream Windows filesystem-sandbox escape in its WASI layer. SculptForge links no WASI and gives plugins no files, network or host functions, so plugins could not reach it; plugin limits (memory, fuel, time) are unchanged.
testers:
  - Any OS: Plugins, run the noise displacer sample; the mesh changes and Undo restores it.
---
Fixes #235
