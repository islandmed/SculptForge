---
whats_new: Scripts and tools that use SculptForge's Python API or command line can be under any licence (new scripting exception).
added: **SculptForge Scripting Exception 1.0** (LICENSE-EXCEPTION.md): programs, scripts, add-ons and tools that use
  SculptForge only through its Python API, command line, MCP server or plug-in SDK may be under any licence;
  SculptForge itself stays GPL-3.0-or-later.
added: **The Python package** (coming to PyPI as `sculptforge`; CPython 3.12-3.14 on Windows x64, macOS universal2 and Linux
  x86_64/aarch64): every command, the mesh, masks, face groups, paint, Parts, layers and selection sets as NumPy
  arrays, type hints, `sculptforge.CommandError`. Saves and exports write to a temporary file and replace the target in
  one step, so a failed or interrupted export never leaves a half-written file. Engine only: no window, no network, no
  port. Guide: The Python package; examples in examples/python.
changed: The `sculptforge` Python module is now a package (`sculptforge/` with the compiled `_core` and type stubs),
  also in the VFX Reference Platform builds; `import sculptforge` is unchanged.
testers:
  - Any OS with Python 3.12-3.14, in a source checkout: `python -m venv v`, `v/bin/pip install .` (Windows: `v\Scripts\pip`),
    then `v/bin/python examples/python/procedural_asset.py out`: out/procedural.glb opens in the app with bumps in a ring.
---
