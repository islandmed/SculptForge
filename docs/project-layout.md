# Project layout

Back to the [README](../README.md). Each folder has its own README: start there.

```
src          The C++ source, in subsystems that depend on each other one way (src/README.md):
  core         Mesh, BVH, brushes & recipes, layers, undo, subdivision, decimation,
               voxel remesh, Gaussian splats, image-to-3D, command registry (no GPU/UI)
  io           File formats: OBJ, FBX, glTF/GLB (+ KHR_gaussian_splatting), PLY, .splat, STL, USDA/USDZ, Blender and Maya exchange
  ai           AI services (free built-in and bring-your-own): HTTPS client, brush assistant, image/text-to-3D services, Gradio client
  link         Local links: AI live mode, phone, Unreal (Python remote execution), Maya
  cli          sculptforge-cli and its MCP server
  render       OpenGL 3.3 / ES 3.2 renderer, splat renderer, offscreen capture
  app          SDL3 + Dear ImGui application, scripted demo recorder, UI test harness
  python       Python module (import sculptforge) and the app's embedded Python host
python       The pip package around that module
sdk          Plugin SDK (sandboxed WebAssembly plugins)
addons       The Blender add-on
integrations Maya shelf script, ComfyUI nodes and workflows
examples     Python examples
android      The Android app build
install      One-line installers
packaging    Linux, Windows, Flatpak, Arch, Rez, Scoop, winget files
cmake        Build modules and pinned dependencies
third_party  Vendored libraries
assets       Fonts, icons, starter meshes, environment maps, built-in materials
docs         This documentation; docs/guide is the user guide
website      Project website (GitHub Pages)
services     The phone-link relay (a Cloudflare Worker)
tests        Unit tests, UI tests, integration tests
bench        Benchmark scenes and results
tools        Checks, generators, test and release helpers
scripts      Release and merge scripts
changes      Release note fragments, one per branch
.github      CI, release and housekeeping workflows, issue and pull request forms (.github/WORKFLOWS.md)
```
