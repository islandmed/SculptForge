# third_party

Code from other projects that is copied into the repository, as its authors wrote it. Anything bigger, or that a
package manager should supply, is not here: it is fetched at configure time, pinned by SHA-256, in
[cmake/Fetch.cmake](../cmake/README.md).

| Folder | What | Version | Licence | Upstream | Used for |
|---|---|---|---|---|---|
| `stb/` | `stb_image.h`, `stb_image_write.h` | commit `013ac3be` | MIT or public domain | https://github.com/nothings/stb | Loading images, writing PNG |
| `ufbx/` | `ufbx.h`, `ufbx.c` | 0.23.1 (commit `5955c5c`) | MIT or public domain | https://github.com/ufbx/ufbx | FBX import |
| `xatlas/` | `xatlas.h`, `xatlas.cpp` | commit `f700c77`, with two local fixes marked `SculptForge:` | MIT | https://github.com/jpcy/xatlas | Automatic UV unwrapping |
| `mikktspace/` | `mikktspace.h`, `mikktspace.c` | commit `3e895b4` | zlib | https://github.com/mmikk/MikkTSpace | Tangent frames for baked normal maps |
| `tinyexr/` | `tinyexr.h` and friends, plus our `tinyexr_impl.cpp` (builds it with stb's zlib) | 1.0.13 | BSD 3-Clause (contains OpenEXR code under its BSD licence) | https://github.com/syoyo/tinyexr | Reading and writing OpenEXR (vector displacement maps) |
| `qrcodegen/` | `qrcodegen.hpp`, `qrcodegen.cpp` | 1.8.0 | MIT | https://github.com/nayuki/QR-Code-generator | The phone link's pairing QR code |
| `lucide/` | `IconsLucide.h`, generated from lucide-static and used with the font `assets/fonts/lucide.ttf` | lucide-static 1.52.0 | ISC | https://lucide.dev | The interface icons |
| `makehuman/` | Only the licence text (`LICENSE.ASSETS.md`) of the base mesh behind the human starter meshes in `assets/starter`; no code | commit `a8bc2d5` | CC0 1.0 | https://github.com/makehumancommunity/makehuman | Starter meshes (see [docs/starter-meshes.md](../docs/starter-meshes.md)) |

Each folder keeps its own licence file and, where it has one, a `SOURCE.txt` saying exactly which upstream files and
revision it came from and what we changed. The full list of everything SculptForge ships or links (fetched
libraries, fonts, assets), with licence texts, is [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md); keep it in step
with this folder.

## Rules

- **Licence first.** Only code under a licence compatible with GPL-3.0-or-later and that we may redistribute. Check
  before you add; copyleft that is not compatible, or "free for non-commercial use", does not belong here.
- **No edits if you can avoid them.** Keep files as upstream wrote them so an update is a copy. A change that is
  needed goes in with a `SculptForge:` comment, and `SOURCE.txt` lists it (xatlas is the only one today).
- **Record the source.** Add a `SOURCE.txt` with the upstream URL, the tag or commit, and the file list, and add the
  component to `THIRD_PARTY_NOTICES.md` with its licence text, in the same pull request.
- **Never someone else's product code.** Nothing here may come from another sculpting application, its SDK or its
  assets (see [CONTRIBUTING.md](../CONTRIBUTING.md)).
- The repository checks skip this folder (it is not our text), so look it over yourself.
- Updating: replace the files, update the version here, in `SOURCE.txt` and in `THIRD_PARTY_NOTICES.md`, and run
  the full tests: these libraries parse untrusted files (images, FBX, EXR).
