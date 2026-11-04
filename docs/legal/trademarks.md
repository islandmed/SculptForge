# Trademarks: how SculptForge names things

SculptForge uses its own names for its own features, and never another company's marks in its own copy.
`tools/check_marks.py` (ctest `marks`) enforces this on every file in the repository.

## The rules

1. **No third-party sculpting-app marks in anything we publish:** the app (menus, tooltips, commands, dialogs,
   brush, alpha and material names), the user guide, the website (including titles, meta and social tags and
   preview images), README, CHANGELOG and release notes, store listings (AUR, Homebrew, winget, Flatpak, Omarchy),
   and Discord, YouTube and outreach templates. That includes the product names themselves and their feature
   names. The patterns are in `tools/check_marks.py`.
2. **Describe by function.** Our names: Forge Spheres, Lit Sphere, Part, Face group, Library, Vertex Paint, Bridge,
   Classic sculpting keys, Classic layout, voxel remesh, Quad Remesh, dynamic topology, Sharp Crease, Clay Block,
   Tendril, Plane Trim. A feature someone knows from another app is found in the command palette by what it does.
3. **No comparison copy.** Never "clone", "replacement", "alternative to ...", or price and feature tables against
   other products. Video titles by others may be quoted as titles.
4. **Never copy** another app's icons, art, documentation text, assets, brush names or SDK code; never decompile
   it; no reader for its proprietary files; never train or test AI on its output.
5. **Code too.** Identifiers and comments use our names. The only exceptions, each kept in one place:
   - `src/io/BridgeProtocol.h`: the folder, file and executable names the exchange protocol needs byte for byte.
   - `src/core/LegacyNames.h`: names older SculptForge versions wrote (projects, brush files, configs), read only
     and mapped to today's names, never shown or written.
   - `integrations/bridge-plugin/`: the export plug-in runs inside the other application and its setup page has to
     name its folders and menus, with the notice below.
6. **History stays.** Git history and tags are not rewritten. Old release pages and assets are removed after a
   release replaces them (`tools/release/scrub_old_releases.sh`).

## Adding to the allowlist

Don't. `tools/marks_allow.txt` lists the three places above. `tools/marks_pending.txt` lists files still waiting
for the code rename and only shrinks; a line marked `// marks-pending` is a script-facing value name that gets
renamed with a read alias.

## The notice

The one sentence that names the other application, used on the Bridge setup page and nowhere else:

ZBrush is a trademark of Maxon. SculptForge is not affiliated with or endorsed by Maxon.

Elsewhere, where a page needs a notice at all: "Product names are trademarks of their respective owners."
