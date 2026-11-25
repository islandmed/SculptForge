# cmake

Helper modules for the top-level `CMakeLists.txt`. The whole build is described there (every source file of every
library is listed in it); this folder holds the parts that are big or shared.

| File | What |
|---|---|
| `Fetch.cmake` | Every third-party archive the build downloads (SDL3, Dear ImGui, GLM, pybind11, Mbed TLS, libdatachannel and its parts, wasmtime), **pinned by URL and SHA-256**, and `sf_fetch_declare()`, the one way they are fetched: downloaded once into a cache, retried with backoff, hash-checked, then extracted. Packagers (Flatpak, AUR) can supply the same files offline |
| `Dependencies.cmake` | Declares the dependencies using those pins: uses an installed SDL3 when one is found (`SCULPTFORGE_USE_SYSTEM_SDL`), else the pinned one |
| `BuildSpeed.cmake` | The edit-build loop: the `mold` linker when installed (`SCULPTFORGE_MOLD`), precompiled standard headers, ccache. Release packaging turns mold off. See [docs/build-speed.md](../docs/build-speed.md) |
| `Wasmtime.cmake` | The WebAssembly runtime for sandboxed plugins, linked statically from the pinned release archive |
| `WebRTC.cmake` | libdatachannel for the phone link's live face feed (`SCULPTFORGE_WEBRTC=OFF` builds without it) |
| `PythonRuntime.cmake` | The CPython the app's Python scripting ships with (`SCULPTFORGE_PYTHON_RUNTIME`) |
| `EmbedFile.cmake`, `EmbedGuide.cmake` | Turn files (fonts, images, starter meshes, HDRIs) and the user guide into C++ arrays inside the binary |
| `MacSign.cmake` | Signs the macOS app bundle during packaging (ad-hoc unless an identity is given) |
| `TestAreas.cmake` | Labels every CTest entry with its area from `tests/areas.map` |
| `mbedtls_user_config.h` | The Mbed TLS configuration the WebRTC build uses |

## Options worth knowing

| Option | Default | |
|---|---|---|
| `SCULPTFORGE_BUILD_APP` | ON | The desktop app |
| `SCULPTFORGE_BUILD_TESTS` | ON | The unit and UI tests |
| `SCULPTFORGE_BUILD_PYTHON` | OFF | The `sculptforge` Python module; also turns on Python scripting in the app |
| `SCULPTFORGE_HTTP` | ON | HTTPS for AI services and the brush gallery (OFF: no network code at all, as in the Python wheels) |
| `SCULPTFORGE_WEBRTC` | ON | The phone link's live face feed |
| `SCULPTFORGE_UPDATE_CHECK` | ON | The once-a-day check for a newer version (package managers turn it off) |
| `SCULPTFORGE_GLES` | OFF (ON for Android) | OpenGL ES 3.2 instead of desktop OpenGL 3.3 |
| `SCULPTFORGE_STUDIO_PROFILE` | empty | A `studio.json` to bake into a studio build ([docs/studios.md](../docs/studios.md)) |

## Rules

- **A new download is a pin:** add `SF_PIN_<name>` (URL and SHA-256) to `Fetch.cmake`, fetch it with
  `sf_fetch_declare()`, and add the component to [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md). Never fetch
  from a branch or without a hash.
- Dependency changes are reviewed closely: they must build offline from the pins, on the oldest supported Linux and on
  macOS and Windows.
- Keep a new feature switchable when it adds a dependency or network code.
