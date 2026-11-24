# addons

Add-ons that run inside other programs and talk to SculptForge. Today: Blender.

## blender/

The **SculptForge Bridge** add-on for Blender 4.2 or newer. Send the selected objects (and, with Blender 5.3 or
newer, Gaussian splats) to SculptForge, sculpt, and get them back: the objects update in place with their transform,
materials and modifiers kept. User guide: [docs/guide/blender.md](../docs/guide/blender.md); design and the exchange
format: [docs/blender.md](../docs/blender.md).

| File | What |
|---|---|
| `sculptforge_bridge/__init__.py` | The add-on: Blender's UI (sidebar tab, Object menu, File > Import / Export), mesh to array conversion |
| `sculptforge_bridge/exchange.py` | The exchange with SculptForge: the shared folder, the manifests, the Bridge files. **No `bpy` here**, so plain Python and NumPy can test it |
| `sculptforge_bridge/splats.py`, `classic_keys.py` | Splat exchange; a Classic sculpting key set for Blender |
| `sculptforge_bridge/blender_manifest.toml` | The extension manifest (id, version, permissions) |
| `build_addon.py` | Packages two zips: the Blender 4.2+ extension and a legacy add-on. `--check` validates the manifest the way Blender's validator does and compiles every file |

The two programs share a per-user folder and poll each other's manifest: no sockets, no ports, and nothing in the
folder is ever run. The C++ side is `src/io/BlenderBridge.*` ([src/io](../src/io/README.md)).

### Build and test

```sh
python3 addons/blender/build_addon.py --out build/blender-addon --check
python3 tests/blender/test_exchange.py --cli build/sculptforge-cli      # needs NumPy; a round trip is the identity
blender -b --factory-startup --python tests/blender/test_in_blender.py -- --cli build/sculptforge-cli   # a real Blender
```

The same checks run in ctest as `blender_addon_package`, `blender_exchange` and (when Blender is installed)
`blender_addon`. The add-on has its own version and its own release tags (`blender-vX.Y.Z`), so it can ship between
SculptForge releases: change the version in `blender_manifest.toml` in the same pull request as the change.
