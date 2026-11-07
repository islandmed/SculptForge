---
changed: Linux packaging: every AppImage build checks that it starts on 4, 16 and 64 KiB memory pages (no bundled
  jemalloc, 64 KiB ELF alignment on ARM64). On Apple Silicon Linux (Asahi) the guide points to the ARM64 build: the
  x86_64 one runs there only through the FEX emulator, whose own jemalloc stops with "Unsupported system page size".
testers:
  - Asahi or another 16 KiB-page ARM64 Linux: the ARM64 AppImage starts and sculpts.
---
Root cause: FEX-Emu 2604 (binfmt for x86_64 on aarch64) is built with jemalloc for 4 KiB pages. Even the AppImage's
static runtime (`--appimage-extract`) aborts under it; our AppImages bundle no jemalloc. In muvm (4 KiB pages) the
x86_64 AppImage gets past FEX. The 0.11.10 ARM64 AppImage starts natively on 16 KiB pages (M1 Pro, offscreen).
