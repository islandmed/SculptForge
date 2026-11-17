#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# ctest's emulator for the iPad's core suite (CMakeLists.txt, SCULPTFORGE_IOS_TESTS): runs a simulator program in the booted
# simulator ($SF_IOS_SIM: its UDID, default "booted"). simctl passes on only SIMCTL_CHILD_* variables: the test's TMPDIR,
# and CI's own (the suite's time limits are looser on CI: tests/TestHarness.h timeLimit).
set -euo pipefail
for v in TMPDIR CI GITHUB_ACTIONS; do
  if [ -n "${!v:-}" ]; then export "SIMCTL_CHILD_$v=${!v}"; fi
done
exec xcrun simctl spawn "${SF_IOS_SIM:-booted}" "$@"
