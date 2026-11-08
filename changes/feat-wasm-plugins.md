---
whats_new: **Plugins**: packs can now bring sandboxed plugins: deformers that reshape the active Part and generators
  that add a new Part. Open one from **Tool > Reshape > Plugins**: its settings, **Apply**, and **Stop** while it
  runs. A plugin sees only a copy of the mesh: no files, network or system access, with time and memory limits.
highlight: Plugins: install sandboxed add-ons from packs (deformers and generators; no file or network access) (thanks
  @bribblej-web, #20)
added: **Sandboxed plugins** (#20): a pack's `plugins/` folder may hold `.sfplugin` files, WebAssembly modules that
  turn a mesh into a mesh (deformers: one undo step, mask-aware; generators: a new Part). They run in the wasmtime
  sandbox with no imports at all (no files, network, clock or system access), a fresh instance per run, a time limit
  (20 s, plus Stop), a work limit and a memory cap; one that crashes or runs too long is turned off with the reason,
  and the mesh is never touched. Their settings come from the plugin's manifest and are drawn by SculptForge; the pack
  review shows each plugin with "Permissions: none". They run only when you run them (never on install, when a
  project opens, from a macro or from an AI agent). `plugin_list`, `plugin_run`, `plugin_enable`; a sample Noise
  Displace plugin, a C header (sdk/plugin) and tools/plugin/sfplugin.py for authors. Thanks to @bribblej-web for the
  request.
security: Plugins: the design and threat model (sandbox escape, denial of service, spoofing, supply chain) are in
  docs/design/plugins.md. The runtime is wasmtime 50.0.1, pinned by SHA-256 and updated every release.
testers:
  - Any OS: install a pack with a plugin (ask in #testers for the sample pack); the review lists the plugin with
    "Permissions: none". Tool > Reshape > Plugins > Noise Displace > Apply: the sphere gets noisy; Ctrl+Z undoes it.
  - Any OS: mask half the model, Apply again: the masked half doesn't move.
  - Windows and macOS: run a plugin on a 1M+ polygon model; the app stays responsive and Stop ends it at once.
---
Security-surface change (code loading): approved by the owner 11-07. Flatpak and the AUR PKGBUILD carry the wasmtime
archive as a source (per architecture), so both ship with plugins on.
