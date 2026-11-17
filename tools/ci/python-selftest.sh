#!/bin/sh
# SculptForge - the package jobs' check that the Python a package ships starts on its own (.github/workflows/packages.yml).
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   tools/ci/python-selftest.sh APP PACKAGE_DIR RUNTIME_DIR
#
# Runs `APP --python-selftest` from the installed package in PACKAGE_DIR with the runtime it was built from
# (RUNTIME_DIR, the unpacked CPython) moved away and no PYTHONHOME/PYTHONPATH, so it can only pass on files the
# package ships. Fails on a build-time fallback home or on a link that points outside the package.
set -eu
app=$1 pkg=$2 rt=$3
mv "$rt" "$rt.away"
rc=0
out=$(env -u PYTHONHOME -u PYTHONPATH "$app" --python-selftest 2>&1) || rc=$?
mv "$rt.away" "$rt"
echo "$out"
[ "$rc" -eq 0 ] || { echo "the Python the package ships didn't start (exit $rc)"; exit 1; }
echo "$out" | grep -q "python fallback home: none" || { echo "a package must not fall back to a build-time Python home"; exit 1; }
if find "$pkg" -type l -lname '/*' | grep .; then
  echo "a package must not link outside itself (above)"
  exit 1
fi
echo "python selftest: ran on the package's own files only ($rt was moved away)"
