# zstd decoder (vendored)

The decompression half of Zstandard, for builds without a system libzstd (Windows, macOS, Android), so a Focus
Detail file written with zstd tiles (core/FocusDetail.cpp) opens on every platform. Builds with a system libzstd
use that instead and never compile these files (CMake option SF_SYSTEM_ZSTD=OFF builds them on Linux too).

- Version: v1.5.7
- Source: https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
- SHA-256 of that tarball (matches the release's zstd-1.5.7.tar.gz.sha256):
  eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
- License: BSD (LICENSE here, unchanged; the release is dual BSD / GPLv2, we take BSD). Also in THIRD_PARTY_NOTICES.md.

## Files (31, byte-identical to the tarball)
- LICENSE; lib/zstd.h, lib/zstd_errors.h; lib/common/ (19 headers and sources) and lib/decompress/ (7) as released.
- lib/zstddeclib.c = the release's build/single_file_libs/zstddeclib-in.c, unchanged: it sets the decoder-only options
  (no legacy formats, no assembly, no error strings, no tracing) and includes the rest. Every file here is in its
  include closure (gcc -M); nothing else is.
- Left out of lib/ (not needed to decode): compress/, dictBuilder/, legacy/, deprecated/, dll/, zdict.h,
  common/pool.*, common/threading.*, common/xxhash.c, decompress/huf_decompress_amd64.S, and the build files
  (BUCK, Makefile, libzstd.mk, libzstd.pc.in, module.modulemap, README.md, .gitignore).

## Verify or update
1. Download the release tarball and its .sha256 from the release page; `sha256sum -c zstd-X.Y.Z.tar.gz.sha256`.
2. For every file here: `cmp` against the tarball's lib/<same path> (LICENSE against LICENSE, lib/zstddeclib.c
   against build/single_file_libs/zstddeclib-in.c). An update copies the same files over these (a new file the
   decoder includes goes in too).
3. Update the version and hash here and in THIRD_PARTY_NOTICES.md; run the focus_detail tests with SF_SYSTEM_ZSTD=OFF.
