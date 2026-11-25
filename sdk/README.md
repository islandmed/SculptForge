# sdk

What you need to extend SculptForge from outside the source tree.

| Folder | For | Start here |
|---|---|---|
| [plugin](plugin/README.md) | Sandboxed WebAssembly plugins: a deformer or a generator that anyone can install without trusting you | `plugin/sculptforge_plugin.h` and `plugin/examples/` |

Other ways to build on SculptForge, none of which needs the SDK: the Python module
([python](../python/README.md)), the command line and MCP server ([src/cli](../src/cli/README.md)),
packs of brushes and macros ([docs/pack-format.md](../docs/pack-format.md)), and the add-ons in
[addons](../addons/README.md) and [integrations](../integrations/README.md).

The SDK files are public domain (CC0). The scripting exception in [LICENSE-EXCEPTION.md](../LICENSE-EXCEPTION.md)
means what you build with them can be under any licence.
