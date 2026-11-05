# Trademarks: how SculptForge names things

SculptForge uses its own names for its own features and never another company's marks, anywhere in this
repository: code, comments, identifiers, file names, tests, docs, history and tools. **This file is the only place
the marks are written out**, so that the check below has a list and old files can still be read.
`tools/check_marks.py` enforces it on every tracked file and path (ctest `marks`, and the CI job "Trademarks" that
every pull request and push to main must pass).

## The rules

1. **No third-party sculpting-app marks** in anything in this repository: the app (menus, tooltips, commands,
   dialogs, brush, alpha and material names), code identifiers and comments, file names, tests, the user guide,
   the website (titles, meta and social tags, preview images), README, CHANGELOG and release notes, store listings,
   design docs, tools, workflows, issue templates and outreach. That includes the product names and their feature
   names (the patterns below).
2. **Describe by function.** Our names: Forge Spheres, Lit Sphere, Part, Face group, Library, Vertex Paint, Bridge,
   Classic sculpting keys, Classic layout, voxel remesh, QRemesh, dynamic topology, Sharp Crease, Clay Block,
   Tendril, Plane Trim, Raise / Lower. A feature someone knows from another app is found in the command palette by
   what it does.
3. **No comparison copy.** Never "clone", "replacement", "alternative to ...", or price and feature tables against
   other products.
4. **Never copy** another app's icons, art, documentation text, assets, brush names or SDK code; never decompile
   it; no reader for its proprietary files and no plug-in that runs inside it; never train or test AI on its output.
5. **Exceptions: none in the tree.** Two things still need the old bytes, and neither spells them:
   - The Bridge file format and its shared folder (`src/io/BridgeProtocol.h`,
     `addons/blender/sculptforge_bridge/bridge_protocol.py`): byte escapes under neutral names, listed below.
   - Files older SculptForge versions wrote (projects, configs, keymaps, radial menus, packs, scripts and macros)
     are read through `src/core/LegacyNames.h`: a hash per old word, listed below, mapped to today's word on load
     and never written. Not carried over: the environment variable for studio Lit Sphere folders (now
     `SCULPTFORGE_LITSPHERE_PATH`) and the old command-line flag for face-group colours (now `--facegroups`).
6. **History stays.** Git history and tags are not rewritten. Old release pages and assets are removed after a
   release replaces them (`tools/release/scrub_old_releases.sh`).

## The patterns

`tools/check_marks.py` reads this block (case-insensitive regular expressions, one per line). Add a mark here; never
add an exception elsewhere.

<!-- marks:begin -->
    z ?brush
    pixologic
    maxon
    \bgoz(?!o)
    gozbrush
    dyna ?mesh
    z ?remesher
    sculptris
    subtools?|\bsub tools?\b
    poly ?paint
    z ?modeler
    lightbox|\blight box\b
    z ?spheres?
    matcaps?|\bmat caps?\b
    fiber ?mesh
    lightcap|\blight cap\b
    \bpixols?\b
    z ?script
    z ?plug(?:in)?s?\b
    \bspotlight\b
    shadow ?box
    nano ?mesh
    array ?mesh
    live ?boolean
    \bz ?(?:add|sub)\b
    insert ?multi ?mesh
    \bimm\b
    poly ?groups?(?!s)
    projection ?master
    hd ?geometry
    decimation ?master
    uv ?master
    clip ?curve
    trim ?curve
    poly ?group ?it
    panel ?loops
    dam_? ?standard
    clay ?buildup
    \bh ?polish\b
    trim ?dynamic
    move ?topological
    \bztools?\b
    \.ztl\b
    mudbox
    \bnomad\b
    sculptgl
    3d-?coat
    gizmo ?3d
    \bpoly ?f(?:rame)?\b
    \btranspose ?all\b
<!-- marks:end -->

## Their interface's labels (#117)

Their exact UI labels are theirs too: SculptForge names its controls by function. Scripts and saved files keep
their old ids (`config.store`, `rgb_intensity`, `focal`, `subdiv_lower`...); only what people read changed.
`tools/check_marks.py` checks these as well (case-insensitive, with the space: code identifiers don't match).

| Was | Now |
|---|---|
| Store Config (Ctrl+Shift+I) | gone: preferences save themselves; Save Layout keeps the layout; `config.store` is Save Preferences Now, no key |
| Restore Standard UI / Restore Custom UI | Reset Layout and Shortcuts / Revert to Saved Layout |
| Enable Customize | Customize Shelves |
| Save UI / Load UI | Export Interface / Import Interface |
| Draw Size / Z Intensity / RGB Intensity / Focal Shift | Size / Intensity / Colour intensity / Falloff |
| Popup Info / Quick Info Opacity | Ctrl+hover help / Help box opacity |
| Lower Res / Higher Res / Del Lower / Del Higher | Lower Level / Higher Level / Delete Lower / Delete Higher |
| Project All / PA Blur | Project Detail / Blur |
| Mask Lasso / Mask Rect | Lasso Mask / Rectangle Mask |
| Radial Count | Radial Copies |
| Group Masked | Group from Mask |

<!-- labels:begin -->
    store ?config
    restore (?:standard|custom) ui
    enable customize
    \b(?:save|load) ui\b
    \bdraw size
    \bz intensity
    \brgb intensity
    focal shift
    popup info
    quick info
    \blower res\b
    \bhigher res\b
    \bdel (?:lower|higher)\b
    \bproject all\b
    \bpa blur\b
    mask lasso
    \bmask rect\b
    radial count
    group masked
<!-- labels:end -->

## Old words read on load (src/core/LegacyNames.h)

FNV-1a 64 of the lower-case word. `tests/test_legacy_names.cpp` checks `LegacyNames.h` against this table.

| Id | Old word | Today | Hash |
|---|---|---|---|
| W1 | `zbrush` | `classic` | `0x77AF1E52C70C5CDB` |
| W2 | `subtool` | `part` | `0x50329458AC585F77` |
| W3 | `zsphere` | `forgesphere` | `0x7A3173B83AFDBBD0` |
| W4 | `polygroup` | `facegroup` | `0xEFC9B9E5E56A64CE` |
| W5 | `polypaint` | `vertexpaint` | `0x06C84E19C9C3D005` |
| W6 | `matcap` | `litsphere` | `0xEB424D5C1B5FCC43` |
| W7 | `lightbox` | `library` | `0xFB0FEB36194770D0` |
| W8 | `damstandard` | `sharpcrease` | `0xBCFC81F111856264` |
| W9 | `claybuildup` | `clayblock` | `0xC1D54DDDFE279385` |
| W10 | `zadd` | `raise` | `0x4265616148F33BEC` |
| W11 | `zsub` | `lower` | `0xDE377B61A12DA42D` |
| W12 | `goz` | `bridge` | `0xD4F75918FABCC503` |
| W13 | `polyf` | `wire` | `0x90F1043555FF3A33` |

## The Bridge format's bytes (src/io/BridgeProtocol.h)

| Constant | Spells |
|---|---|
| `kExt`, `kExtLower`, `SF_BRIDGE_MESH_EXTS` | `.GoZ`, `.goz` |
| `kHeader`, `kMagic` | `GoZb 1.0 ZBrush GoZ Binary`, `GoZb` |
| `kNamePrefix` | `GoZMesh_` |
| `kVendorFolder`, `kMacRoot` | `Pixologic`, `/Users/Shared/Pixologic` |
| `kInboxFolder`, `kProjectsFolder`, `kAppsFolder` | `GoZZBrush`, `GoZProjects`, `GoZApps` |
| `kObjectList`, `kApplicationFile`, `kProjectPathFile`, `kInfoFile`, `kConfigFile` | `GoZ_ObjectList.txt`, `GoZ_Application.txt`, `GoZ_ProjectPath.txt`, `GoZ_Info.txt`, `GoZ_Config.txt` |
| `kNotifierWin`, `kNotifierMac` | `GoZZBrushFromApp.exe`, `.app` |
| `kWinInstallDirs`, `kWinExe`, `kMacApp`, `kAppNameLower` | `C:\Program Files\Pixologic`, `C:\Program Files\Maxon`, `ZBrush.exe`, `ZBrush.app`, `zbrush` |

## The notice

ZBrush is a trademark of Maxon. SculptForge is not affiliated with or endorsed by Maxon. Where a page needs a notice
at all, it says: "Product names are trademarks of their respective owners."
