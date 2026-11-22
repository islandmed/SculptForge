# zstd decoder (vendored)

The decompression half of Zstandard, for builds without a system libzstd (Windows, macOS, Android), so a Focus
Detail file written with zstd tiles (core/FocusDetail.cpp) opens on every platform. Builds with a system libzstd
use that instead and never compile these files.

- Release: v1.5.7, https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
- SHA-256 of that tarball: eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
- License: BSD (LICENSE here; the release is dual BSD / GPLv2, we take BSD)
- Files: lib/zstd.h, lib/zstd_errors.h, lib/common/ and lib/decompress/ as released (only what the decoder includes),
  and lib/zstddeclib.c = the release's build/single_file_libs/zstddeclib-in.c unchanged (it includes the rest).

Update: download the new release tarball, check its SHA-256 against the release's .sha256 file, copy the same files
over these (a new file the decoder includes goes in too), update the version and hash above and in
THIRD_PARTY_NOTICES.md, and run the Focus Detail tests (the focus_detail ones).
