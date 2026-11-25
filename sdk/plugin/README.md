# SculptForge plugin SDK

Write a tool for SculptForge in any language that compiles to WebAssembly (C, C++, Rust, Zig...), and anyone can
install it without having to trust you. A plugin is a **sandboxed WebAssembly module** that turns a mesh into a mesh:

- **Deformer**: gets the active Part's mesh and returns new positions for its vertices. One undo step; masked and hidden
  vertices stay where they are.
- **Generator**: gets the same mesh and returns a new one, added as a new Part.

This folder is the ABI and a working sample. The design, limits and threat model are in
[docs/design/plugins.md](../../docs/design/plugins.md); how a plugin is installed in a pack is in
[docs/pack-format.md](../../docs/pack-format.md).

| File | What |
|---|---|
| `sculptforge_plugin.h` | The C header: `sf_input`, `sf_output`, `SF_PLUGIN_ABI` (1). Public domain (CC0): copy it into your plugin |
| `examples/noise_displace.c` | A deformer in C: moves every vertex along its normal by smooth noise. The manifest is [tests/data/plugins/noise_displace.json](../../tests/data/plugins/noise_displace.json); the same plugin in WebAssembly text is next to it |

## What a plugin is

One WebAssembly 1.0 module (wasm32) with **no imports at all**: no WASI, no files, no network, no clock, no other
programs. It exports:

| Export | |
|---|---|
| `memory` | its linear memory |
| `sf_alloc(bytes)` | returns an 8-byte aligned block of that size in its memory, or 0 |
| `sf_run(input)` | reads the `sf_input` at that address and returns the address of an `sf_output` |

and carries its manifest (strict JSON: `id`, `name`, `version`, `kind`, `params` with `number`, `int` and `bool`
types, min, max and default; `permissions` must be empty) in a custom section named `sculptforge.plugin`. SculptForge
draws the parameter panel itself from the manifest. The input holds the vertex positions, normals, mask, triangles and
the parameter values; the output is new positions (deformer) or a new mesh (generator), a status and a message.

## Build one

```sh
cd sdk/plugin/examples
clang --target=wasm32 -O2 -nostdlib -I.. -Wl,--no-entry -Wl,--export=sf_alloc -Wl,--export=sf_run \
      -o noise_displace.wasm noise_displace.c
python3 ../../../tools/plugin/sfplugin.py noise_displace.wasm ../../../tests/data/plugins/noise_displace.json -o "Noise Displace.sfplugin"
```

`tools/plugin/sfplugin.py` checks what SculptForge checks before it installs one (no imports, the three exports, a
valid manifest) and adds the manifest section. Put the `.sfplugin` in a pack's `plugins/` folder and install the pack
with **File > Install Pack...**; **Tool > Reshape > Plugins** then lists it with its settings and "Permissions: none".

## The safety model, in short

A plugin is code from a stranger, so it is treated like a hostile file:

- It runs **only** when the user presses Apply or Add Part (or runs `plugin_run` from the console, the CLI or Python).
  Never on install, never when a project opens, never from a macro, never from an AI agent: the MCP server hides every
  `plugin_*` command.
- Every run is a **fresh instance**: nothing survives between runs. It gets a fuel budget, a memory limit (256 MB plus
  4x the input, 4 GB at most), a 20 second deadline with a Stop button, a 512 KB stack and a 16 MB module size cap.
- Its result is **read as hostile data**: counts and addresses checked against its memory, positions finite, triangle
  indices in range. Any failure leaves the mesh untouched. A plugin that traps or runs out of fuel or time is turned
  off until the user turns it on again.
- Runtime: the [wasmtime](https://wasmtime.dev) C API, linked statically (`cmake/Wasmtime.cmake`).

The host side is `src/core/Plugin.h/.cpp`.

## Test it

`tests/test_plugins.cpp` covers the manifest rules, bad modules, a runaway loop and the sample; `tests/ui/plugins.sfui`
drives the panel. Run them with the unit-test program and `ctest -R plugins` ([tests/README.md](../../tests/README.md)).
Questions and plugins you want listed: [GitHub issues](https://github.com/islandmed/SculptForge/issues) or the
[Discord](https://discord.gg/gWrYkEmQhH).
