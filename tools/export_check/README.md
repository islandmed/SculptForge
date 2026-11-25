# tools/export_check

Checks SculptForge's textured exports with other people's tools instead of our own loaders: a file our reader accepts
can still be wrong for the program it is meant for.

| Format | Checked with |
|---|---|
| glTF / GLB | The Khronos glTF validator (`validate_gltf.js`, npm `gltf-validator`) |
| USD / USDZ | OpenUSD's validation framework (`usdcheck.py`, pip `usd-core`) |
| FBX | [ufbx](../../third_party/README.md) in strict mode (`fbxdump.c` prints every corner with its UV, and the materials and textures it finds) |
| all | The texture is sampled the right way up (`orient.py`): a checker texture goes through each format's UVs (OBJ, USD and FBX have v up; glTF has v down) and is compared with SculptForge's own sampling |

## Run it

After a [build](../../docs/building.md):

```sh
tools/export_check/run.sh build [work-folder]
```

It needs Node and npm, a C compiler, and either `uv` or `python3 -m venv` (it installs the validators in a throwaway
environment, not on your system). It downloads the validators, so it is not part of `ctest`; run it when you change an exporter
(`src/io`, for example `TextureExport.h`), and say so in the pull request.
