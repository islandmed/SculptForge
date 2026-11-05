#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   tools/ci/fetch.sh URL SHA256      prints the path of a checked local copy of URL
#
# CI's downloads of tools (ccache, uv): kept in SF_DEPS_CACHE (default .deps, the folder each job caches with
# actions/cache, key deps-...; cmake/Fetch.cmake keeps the build's own archives there too), named by their hash, and
# downloaded with retries and backoff when missing: 0.11.1's packages died on GitHub 504s.
set -euo pipefail
[ $# -eq 2 ] || { echo "usage: $0 URL SHA256" >&2; exit 2; }
url=$1 sum=$2
dir=${SF_DEPS_CACHE:-.deps}
mkdir -p "$dir"
f=$dir/$sum-${url##*/}
sha() { if command -v sha256sum >/dev/null; then sha256sum "$1" | cut -d' ' -f1; else shasum -a 256 "$1" | cut -d' ' -f1; fi; }
# --retry alone already retries 5xx answers; --retry-all-errors (curl 7.71+; not EL8's 7.61) also covers resets.
all=; curl --help all 2>/dev/null | grep -q -- --retry-all-errors && all=--retry-all-errors
if [ ! -f "$f" ] || [ "$(sha "$f")" != "$sum" ]; then
    # curl's own retries back off 1-16 s; a longer outage gets two more rounds, 30 s and 90 s later.
    for wait in 0 30 90 x; do
        [ "$wait" != x ] || { echo "can't download $url" >&2; exit 1; }
        sleep "$wait"
        curl -fsSL --retry 5 $all --connect-timeout 30 -o "$f.part" "$url" && break
    done
    got=$(sha "$f.part")
    [ "$got" = "$sum" ] || { rm -f "$f.part"; echo "$url: SHA-256 $got, expected $sum" >&2; exit 1; }
    mv "$f.part" "$f"
fi
echo "$f"
