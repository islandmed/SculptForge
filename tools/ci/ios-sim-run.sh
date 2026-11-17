#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ctest's emulator for the iPad's core suite (CMakeLists.txt, SCULPTFORGE_IOS_TESTS): runs a simulator program in the booted
# simulator ($SF_IOS_SIM: its UDID, default "booted"), with the test's TMPDIR (simctl passes SIMCTL_CHILD_* variables on).
set -euo pipefail
[ -n "${TMPDIR:-}" ] && export SIMCTL_CHILD_TMPDIR="$TMPDIR"
exec xcrun simctl spawn "${SF_IOS_SIM:-booted}" "$@"
