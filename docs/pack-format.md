# The SculptForge pack format (.sfpack)

A pack shares brushes, alphas, Lit Spheres, Material Recipes, VDM brushes, keymaps, pie menus and macros as one
file. It is **data only**: SculptForge never runs anything from a pack, nothing happens when it is installed or
when a project opens, and nothing in it reaches the network. Users install packs with **File > Install Pack...**
(a review shows what's inside first), `pack_install path=...` in `sculptforge-cli` or Python; see the user guide
chapter [Packs and macros](guide/packs-and-macros.md).

The reader is in `src/core/Pack.cpp`, the macro rules in `src/core/Macro.cpp`, and the tests that feed it
hostile archives in `tests/test_packs.cpp`.

## The archive

A `.sfpack` is a ZIP archive. Entries may be stored or deflated. The layout is fixed:

```
pack.json                      the manifest (required)
LICENSE.txt                    the license text (optional; LICENSE or LICENSE.md also work; at most one)
preview.png                    a preview image (optional; or preview.jpg; must be named in pack.json)
brushes/<name>.sfbrush         brush recipes (core/BrushLibrary.h)
alphas/<name>.png|.jpg|.jpeg   brush alphas
matcaps/<name>.png|.jpg|.jpeg  Lit Sphere images
materials/<name>.sfmaterial    Material Recipes (core/MaterialRecipe.h)
vdms/<name>.sfvdm              vector displacement maps (core/Vdm.h)
keymaps/<name>.json            keymaps (below)
piemenus/<name>.json           pie menus, as Preferences > Input > Radial Menus exports them
macros/<name>.sfmacro          macros (below)
```

Every file outside `pack.json`, the license and the preview must be listed in `pack.json`'s `contents`, directly
in its type's folder, with its type's extension. Anything else is refused.

## pack.json

```json
{
  "format": "sculptforge-pack",
  "formatVersion": 1,
  "id": "supporter-2026-11",
  "name": "Monthly pack November 2026",
  "version": "1.0",
  "author": "The SculptForge developer",
  "license": "CC-BY-4.0",
  "description": "Twelve hard-surface brushes, four alphas and a clean-up macro.",
  "minAppVersion": "0.10.6",
  "preview": "preview.png",
  "channel": "insider",
  "contents": [
    {"type": "brush", "path": "brushes/Panel Line.sfbrush"},
    {"type": "alpha", "path": "alphas/Rivets.png"},
    {"type": "macro", "path": "macros/Clean Up.sfmacro"}
  ]
}
```

| Key | Required | Rules |
|---|---|---|
| `format` | yes | `"sculptforge-pack"` |
| `formatVersion` | yes | `1`. A newer number is refused (a newer SculptForge may read it). |
| `id` | yes | 1-64 characters: lower-case letters, digits, `.`, `_`, `-`; starts with a letter or digit; no `..`. The pack's folder name; a pack with the same id replaces the installed one. |
| `name` | yes | At most 64 bytes. Shown next to every item the pack brings. |
| `version` | yes | Starts with a digit; letters, digits, `.`, `-`, `+`; at most 32 bytes. |
| `author` | yes | At most 64 bytes. |
| `license` | yes | At most 64 bytes, for example `CC-BY-4.0` or `CC0-1.0`. |
| `description` | no | At most 1000 bytes; may hold line breaks. |
| `minAppVersion` | no | The oldest SculptForge that may install it; newer than the running app: refused. |
| `preview` | no | `preview.png` or `preview.jpg`, which must be in the archive. |
| `channel` | no | Lower-case letters, digits and `-`. `"insider"` marks the monthly pack or an Insider drop (shown as **Member pack**). **Display only**: it changes a label, nothing else; there is no DRM and anyone can install the file. |
| `contents` | yes | A list of `{"type", "path"}`. Types: `brush`, `alpha`, `matcap`, `material`, `vdm`, `keymap`, `piemenu`, `macro`. |

Text values must be valid UTF-8 without control characters. The JSON is parsed strictly: a key given twice,
an unknown key, a number JSON doesn't allow or trailing text is an error.

## What every pack is checked for

Every pack is treated as hostile input. It is refused, with a message naming the file and the reason, when:

- **Paths**: a name is absolute, uses `\`, a drive letter or `:`, starts with `~`, has an empty, `.` or `..` part,
  a hidden part (`.x`), a part that starts or ends with a space or dot, a character Windows can't hold
  (`< > " | ? *`), a control character, invalid UTF-8, a NUL byte, a Windows device name (`CON`, `NUL`, `COM1`
  ...), more than one folder level, or more than 160 bytes.
- **Entries**: an entry is a symbolic link, a device or other special file, encrypted, compressed with anything
  but stored or deflate, spread over several disks or a ZIP64 entry; two entries share a name (ignoring case, as
  Windows and macOS do); two entries overlap in the file (a zip-bomb trick); the local header disagrees with the
  directory; the CRC doesn't match; or there is data after the archive's end.
- **Sizes**: the archive is over 128 MB, it holds over 1000 entries, one file unpacks to over 64 MB, all files
  together to over 256 MB, a file over 64 KB unpacks to more than 100 times its packed size (or the whole pack
  does), or a deflated stream unpacks to more than its stated size (it is decoded into a buffer of exactly that
  size).
- **Types**: a file isn't listed in `pack.json`, isn't in its type's folder or has the wrong extension, or a
  listed file is missing.
- **Contents**: each file is parsed as what it claims to be. Brushes, Material Recipes, keymaps and pie menus are
  strict JSON within their size caps; images are PNG or JPEG whose bytes match the extension, at most 4096 pixels
  a side (the preview 2048), and must decode; VDMs must have the `.sfvdm` header and exactly the size it states;
  the license is plain text up to 64 KB; macros pass the macro rules below.

Installing unpacks into a fresh staging folder inside `<app data>/packs/`, checks every destination again, then
swaps it in, so a failed install leaves the previous version as it was. Packs never write anywhere else and
never overwrite built-in or user files.

## Keymaps

```json
{"format": "sculptforge-keymap", "version": 1, "name": "Hard Surface Keys",
 "shortcuts": {"brush.clay": "C", "mask.invert": "Ctrl+Shift+K"}}
```

At most 512 shortcuts. Keys are action ids (as **Preferences > Hotkeys** lists them) and key chords. They are only
applied when the user clicks **Apply keymap**; unknown actions and chords are left out.

## Macros (.sfmacro)

Plain text in the command language of `sculptforge-cli` (`docs/guide/scripting.md`): one command per line, `#`
comments, optional `# name:` and `# description:` header lines. At most 64 KB and 500 commands.

- Only SculptForge's own commands, and not those that reach the network or another program (AI services, the
  brush gallery, Unreal, Blender, Maya and ZBrush exchange, splat training), install, list or remove packs, or run
  macros. Parameters naming URLs, endpoints, services or programs are refused.
- File values (`path=`, `folder=`, `file=`, `out=`, `exr=` ...) are either a path relative to the project folder,
  without `..`, `~`, `$`, `%`, drive letters or a leading slash (resolved, symbolic links followed, and checked to
  stay inside the folder), or `?`, which asks for a file in a dialog when the macro runs. `preset=` may only name a
  built-in preset.
- In the app, a macro shows its whole command list and asks before it first runs, and again after its text
  changes (confirmations are kept by SHA-256 of the text). `sculptforge-cli` and Python run what the user asked.
  AI agents on `sculptforge-cli --mcp` can't install, remove or switch packs.
