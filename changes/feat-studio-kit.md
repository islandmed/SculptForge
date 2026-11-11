---
whats_new: **Bug report packs**: **Help > Create bug report pack...** writes one .zip for a bug report without your
  model: version, GPU and driver, changed settings, the end of the log, your last commands and a stand-in mesh with
  your model's counts but none of its shape. Paths, file names, links and your Parts', layers', project's, packs' and
  account's names are replaced; read the log and actions before you post, as other names can remain. You see every
  file before saving; nothing is sent.
added: **Help > Create bug report pack...**: a local .zip for bug reports with the app and system details, the
  settings that differ from the defaults, the log's last 200 lines and the last 200 commands and strokes (command
  values kept only when they are numbers or on/off), per-Part mesh statistics (counts, pieces, genus, holes,
  levels) and a generated stand-in mesh with the same counts. No vertex positions, textures or images go in. Paths,
  file names, links, e-mail addresses, quoted text and the names of the Parts, layers, Part folders, selection sets,
  custom brushes, project, packs, macros, studio and account are replaced; other names typed into the log can
  remain, so the preview shows each file in full (read log.txt and actions.txt before posting) and any of them can
  be left out. `bug_report_pack path=...` writes the same pack from a script.
added: **Studio kit** (packaging/studio-kit, docs/studios.md): a studio builds its own SculptForge on its own CI or an
  air-gapped machine, with nothing sent to us. It checks a release's signature and checksums, pins the version,
  adds the studio's brushes, keymaps, macros, plugins, preferences and logo to the unchanged signed Linux and Windows
  archives (macOS: a folder beside the app), and writes byte-identical outputs with a CycloneDX SBOM and checksums.
testers:
  - Any OS: Help > Create bug report pack...; untick Log, Save; open the .zip: no log.txt, and none of your Part or
    file names anywhere in it. Open proxy.ply in any viewer: spheres/tori, not your model.
  - Windows: Help > Create bug report pack... at 150 % and 200 % UI scale: the window fits and every "Show what's
    inside" opens.
---
The UI test path change (PanelBrushLibrary.cpp) re-installs a studio's packs into a UI test's own pack folder; it
only runs under --ui-test.
