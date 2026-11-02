#!/bin/sh
# Installs everything needed to build SculptForge on Debian/Ubuntu or Fedora/RHEL-family systems.
set -eu
if command -v apt-get >/dev/null 2>&1; then
  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config git file ca-certificates \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl1-mesa-dev libgl1-mesa-dev \
    libdbus-1-dev libudev-dev libcurl4-openssl-dev python3-dev
elif command -v dnf >/dev/null 2>&1; then
  dnf -y install dnf-plugins-core
  # Extra development repositories: "crb" on EL9+, "powertools" on EL8.
  dnf config-manager --set-enabled crb 2>/dev/null || dnf config-manager --set-enabled powertools 2>/dev/null || true
  # --nobest: a half-synced mirror (appstream gcc-c++ newer than baseos libstdc++) must not stop the build.
  dnf -y install --nobest gcc-c++ cmake make pkgconf-pkg-config git file rpm-build \
    libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel libXScrnSaver-devel \
    libXtst-devel libxkbcommon-devel wayland-devel wayland-protocols-devel mesa-libEGL-devel mesa-libGL-devel \
    dbus-devel systemd-devel libcurl-devel
  # Window title bars on Wayland desktops without server-side decorations (GNOME); EL9+ has it in CRB, EL8 doesn't.
  dnf -y install libdecor-devel 2>/dev/null || true
  # EL8/EL9 ship older system compilers. GCC 14 matches the VFX Reference Platform (CY2026/CY2027).
  dnf -y install gcc-toolset-14 2>/dev/null || dnf -y install gcc-toolset-13 2>/dev/null || true
else
  echo "Unsupported distribution: install a C++20 compiler, CMake 3.24+, X11/Wayland/GL development headers and libcurl." >&2
  exit 1
fi
