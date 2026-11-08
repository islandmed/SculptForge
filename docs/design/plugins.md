# Sandboxed plugins (#20)

Status: phase 1 built (`feat/wasm-plugins`); phase 2 is a design only. Requested by @bribblej-web in #20 (a path
tracer they are bringing over from their own sculpting program). Security-surface change: approved by the release
coordinator before merge.

The goal: plugins **anyone can install without fear**. A plugin is code from a stranger, so it is treated the way we
treat a hostile file, plus the things only code can do (loop, allocate, lie about its results). Usability and
safety were weighed together: phase 1 gives plugins exactly what a mesh tool needs and nothing else, and the user
never has to judge a permission prompt to stay safe.

## Phase 1: mesh in, mesh out

| | |
|---|---|
| Kinds | **deformer** (new positions for the active Part's vertices, one undo step; the mask and hidden faces hold their vertices) and **generator** (a new mesh, added as a new Part) |
| Format | `.sfplugin` = one WebAssembly 1.0 core module, its manifest (JSON) in a custom section named `sculptforge.plugin` |
| Install | only inside a pack: `plugins/<name>.sfplugin` (docs/pack-format.md). The pack review shows each plugin's name, kind, size, settings and "Permissions: none". One-click remove = removing (or turning off) the pack |
| Runtime | [wasmtime](https://wasmtime.dev) C API 50.0.1, linked statically, pinned by SHA-256 per platform (cmake/Fetch.cmake); updated every release (docs/releasing.md) |
| UI | the plugin's parameters (number, int, bool; min, max, default, help) come from its manifest and **we draw them** (PanelPlugins.cpp): Tool > Reshape > Plugins, or the pack's details in Library > Packs |
| Runs | only when the user presses Apply / Add Part, or types `plugin_run` in the console, sculptforge-cli or Python. Never on install, never when a project opens, never from a macro, never from an AI agent (MCP and AI live mode don't list it) |
| ABI | src/core/Plugin.h, sdk/plugin/sculptforge_plugin.h (C), a sample in WebAssembly text (tests/data/plugins/noise_displace.wat) and in C (sdk/plugin/examples/noise_displace.c); `tools/plugin/sfplugin.py` adds the manifest |

### How a run works

1. The UI thread copies the active Part's positions, normals, mask and triangles (`PluginInput`).
2. A worker thread gets a **fresh store and instance** of the (cached, compiled) module: nothing survives from one
   run to the next. The store has a fuel budget, a memory limiter and an epoch deadline.
3. The host calls `sf_alloc(bytes)`, checks the block is aligned and inside the plugin's memory, writes the input,
   and calls `sf_run(input)` once.
4. The result record is read as hostile data: every count and address checked in 64-bit arithmetic against the
   memory's size, counts against the input (a deformer returns exactly one position per vertex and no triangles),
   every position finite and within 1e9, every triangle index in range with no repeats, caps of 64M vertices and
   128M triangles for a generator. The data is copied out; the store is dropped.
5. Back on the UI thread the result is committed through the same path as Tool > Reshape (Sculptor, one undo
   step, sculpt layers respected) or added as a Part. If the Part changed meanwhile, nothing is applied.

Any failure leaves the mesh untouched (nothing is written until step 5). A plugin that **traps** (out-of-bounds
access, unreachable, stack overflow, bad call), **runs out of fuel** or **out of time** is turned off, with the
reason, until the user turns it on again (keyed by the module's SHA-256: an updated plugin gets a fresh start). A
broken result or the plugin's own error message is reported without turning it off.

### Limits

| Limit | Value | Why |
|---|---|---|
| Module size | 16 MB | compile time and memory are bounded by input size |
| Memory | 256 MB + 4x the input, at most 4 GB (wasm32) | room to work, never the whole machine; growth past it fails (`memory.grow` returns -1) |
| Fuel | 2 billion + 20,000 per vertex and triangle | a runaway loop stops even when the clock is coarse |
| Time | 20 s wall clock; **Stop** button | the user is never stuck; epoch interruption checked every 10 ms |
| Stack | 512 KB of wasm stack | deep recursion traps instead of crashing |
| Instances / tables / memories | 1 / 4 / 1, 100,000 table elements | no resource multiplication |
| Parameters | 32, finite numbers, clamped to min..max before the plugin sees them | |

## Threat model

Assets: the user's machine and files, their accounts and keys (keychain), their work (the open scene and its
history), their trust in what a pack says it is.

Attacker: whoever wrote or tampered with a `.sfplugin` (inside a pack shared on Discord, a website, the brush
gallery's neighbours). They control every byte of the module and its manifest and every value it returns. They
don't control SculptForge's code, the user's clicks or the pinned runtime.

### Escape (code execution or file/network access outside the sandbox)

- **No capabilities to abuse.** A module that imports anything (a WASI function, a host function, a memory, a
  table, a global) is refused at review, by our own section reader and again by wasmtime before instantiating. With
  no imports, the only way out is through the host's two calls and the result record: there is no file, socket,
  clock, random source, environment, process or thread a plugin can name.
- **The ABI surface is tiny and checked.** Two exported functions with one i32 each; the host reads only the
  48-byte input it wrote and the 32-byte output record, all bounds checked in 64 bits before any copy; messages are
  cleaned (control characters, invalid UTF-8) and capped.
- **The runtime.** Wasmtime is a widely used, security-focused WebAssembly runtime (fuzzed continuously in
  OSS-Fuzz, with a published security policy and advisories). Our config turns off
  what we don't need, shrinking the code a module can reach: no threads or shared memory, no memory64, no
  multi-memory, no GC proposal, no component model. NaN canonicalization keeps results deterministic across CPUs.
- **No signal handlers in our process.** `signals_based_traps` is off: wasmtime uses explicit bounds checks
  instead of guard pages and SIGSEGV handlers, so the app's crash handler and wasmtime never fight over a fault,
  and a wasmtime bug can't turn a host crash into a "trap". (Cost: a few % on memory-heavy plugins.)
- **Residual risk:** a wasmtime or Cranelift miscompilation bug (sandbox escape class). Mitigations: pinned and
  updated every release (the release checklist and the weekly upstream watch read wasmtime's security
  advisories); packs are refused if the module doesn't validate; a hostile module still gains only what the app
  process can do, which phase 2 doesn't widen. Further hardening, if ever needed: run plugins in a separate,
  OS-sandboxed process (seccomp / AppContainer / App Sandbox) speaking the same ABI over a pipe.

### Denial of service

- Infinite loops: fuel and the wall-clock epoch, plus the Stop button; a start function is covered too (it runs
  while instantiating, under the same store limits).
- Memory: the store limiter caps linear memory and tables; host-side copies are sized by the checked output counts
  (and capped), never by a value the plugin can inflate past its own memory.
- Stack: the wasm stack limit traps; the host never recurses on plugin data.
- Compile bombs: modules over 16 MB are refused before compiling; compiling happens only on a user's run, never on
  install or project open; compiled modules are cached by SHA-256 (at most 8).
- UI freeze: runs are on a worker thread; the UI stays live and shows elapsed time with Stop.
- Repeated failures: the plugin is turned off after a trap or timeout until the user turns it on again.

### Spoofing (a plugin pretending to be something it isn't)

- The review screen shows the pack's and each plugin's name, author, version, kind, size and settings, with
  "Permissions: none" from the manifest, which phase 1 refuses if it asks for anything.
- Plugins never draw UI: no fake dialogs, no "enter your API key", no lookalike buttons. Labels and help text are
  plain text, cleaned and capped.
- A pack is a pack: same id replaces only after the review; names show with the pack's name next to them.
- Not in phase 1: signatures. A plugin's author is what its manifest says. When packs become signed (the brush
  gallery's index already pins SHA-256), plugins inherit it.

### Supply chain

- Our side: wasmtime is fetched from the Bytecode Alliance's GitHub releases by URL **and SHA-256** (cmake/Fetch.cmake,
  the same mechanism as SDL, ImGui and Mbed TLS); a changed file stops the build. Offline packagers (Flatpak, AUR)
  pass the same archive as a source or build without plugins (`Plugins: off` at configure; plugins then say this
  build can't run them).
- Updating it is part of every release (docs/releasing.md: bump `SF_WASMTIME_VERSION` and the five SHA-256s,
  read the release notes for security fixes, run the plugin tests).
- The plugin's side: a pack is checked completely before anything is written (docs/pack-format.md); its plugins are
  validated, and the bytes run are the bytes reviewed (the installed file's SHA-256 is checked again before a run).

### What the tests cover (tests/test_plugins.cpp)

Manifest rules (unknown keys, permissions, kinds, ids, parameter ranges and duplicates, a newer format); the
reader against non-modules, components, truncated sections, over-long LEB128s, oversized files, missing or doubled
manifests, every kind of import and missing exports; infinite loops (time and fuel), a looping start function,
unbounded recursion, Stop; memory growth past the cap, an `sf_alloc` that lies, a mesh bigger than the cap,
out-of-bounds loads; broken results (wrong counts, addresses outside memory or misaligned, a wrong ABI, NaN, indices
past the end, repeated indices, no triangles, a plugin's own error message, a wrong signature); random damage to a
good module (3,000 mutated modules read, and run under tight limits when they validate) and every result field set to extremes; the
sample through a pack: review, install, run, undo, the mask, turning off after a timeout and on again, pack
disable and remove; macros refusing plugin commands; and the call overhead below.

## Call overhead (1M vertices, 2M triangles)

`plugin_call_overhead_1m` measures a plugin that returns its input unchanged: the host copies the mesh in (52 MB),
calls it, checks and copies the positions out. Laptop (Apple M1 Pro under Linux, release build, best of 3, module
already compiled):

| | ms |
|---|---|
| A plugin call that returns the mesh unchanged (copy in, call, check, copy out) | 7.1 |
| A plain copy of the same data in C++ (for scale) | 3.8 |
| Noise Displace (8 hashed lattice values and a blend per vertex, fuel metering on) | 129.7 |

The first run of a module in a session also compiles it (the compiled module is then cached by SHA-256).

The overhead is a copy of the mesh plus a check, so a plugin costs about what a whole-mesh deformation's own
memory traffic does; the plugin's own math is then the cost (compiled to native code by Cranelift).

## Phase 2 (design only): a read-only "render output" permission

For #20's path tracer: a plugin that sees the scene and returns an image, never touching the scene.

- **Permission:** `"permissions": ["render-output"]`. The review screen shows it in words ("Reads the scene: meshes,
  materials, lights and camera. Returns an image. Can't change your scene, files or network."), and the user must
  tick it before Install. Unknown permissions stay refused.
- **Input:** a read-only snapshot the host serializes into the plugin's memory: every visible Part's triangles and
  positions (world space), normals, vertex colours, the material per Part (base colour, roughness, metallic,
  emission: Material Recipe values), the lights and environment (HDRI as a downsampled RGB float image), the camera
  (view, projection, size). Textures as RGBA8 with size caps. No file paths, names beyond Part names, or user data.
- **Output:** an RGBA float image of the requested size (checked: exact size, finite values) shown in a render
  window and saved only by the user (File > Save Render). Progressive rendering: the host calls `sf_render(input,
  pass)` repeatedly, each call a short fuel and time slice, and shows each pass, so a long render stays responsive
  and stoppable without threads in the plugin.
- **Limits:** memory cap from the snapshot size (scene + 4 x image, at most 4 GB), per-pass time slice (250 ms),
  total time set by the user in the render window.
- **Not in phase 2:** plugin-drawn UI, file or network access, threads (wasm threads stay off; parallelism is the
  host running passes for tiles in separate instances, each with its own limits).

## Credits

@bribblej-web (#20), for the request and the path-tracer use case.
