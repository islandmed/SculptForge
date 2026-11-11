#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# packaging/linux/apt-retry.sh (ctest apt_retry): a stand-in apt-get that fails while the sources still name the Azure mirror (the
# 11-11 DNS failure), in a sandbox /etc/apt. update and install must succeed through archive.ubuntu.com; a mirror that is down
# everywhere must fail after the last mirror; a missing package must fail at once, without trying the other mirrors.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=$(mktemp -d)
trap 'rm -rf "$S"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
mkdir -p "$S/apt/sources.list.d"
cat > "$S/apt-get" <<'STUB'
#!/bin/sh
echo "$*" >> "$STUB_LOG"
case "$STUB_MODE" in
  azure) grep -rq azure.archive.ubuntu.com "$APT_RETRY_ETC" && { echo "E: Failed to fetch http://azure.archive.ubuntu.com: Temporary failure resolving" >&2; exit 100; } ;;
  down) echo "E: Failed to fetch: Temporary failure resolving" >&2; exit 100 ;;
  missing) echo "E: Unable to locate package nosuchpkg" >&2; exit 100 ;;
esac
exit 0
STUB
chmod +x "$S/apt-get"
export APT_RETRY_ETC=$S/apt APT_RETRY_APT_GET=$S/apt-get APT_RETRY_PAUSE=0 STUB_LOG=$S/log
reset_sources() {
  printf 'deb http://azure.archive.ubuntu.com/ubuntu/ jammy main\ndeb http://security.ubuntu.com/ubuntu/ jammy-security main\n' > "$S/apt/sources.list"
  printf 'Types: deb\nURIs: http://azure.archive.ubuntu.com/ubuntu/\nSuites: noble\n' > "$S/apt/sources.list.d/ubuntu.sources"
  : > "$S/log"
}
run() { STUB_MODE=$1 sh "$ROOT/packaging/linux/apt-retry.sh" "${@:2}" >"$S/out" 2>&1; }

reset_sources
run azure install -y foo || { cat "$S/out"; fail "install did not survive the Azure mirror"; }
grep -q 'archive.ubuntu.com/ubuntu/ jammy main' "$S/apt/sources.list" || fail "sources.list was not switched to archive.ubuntu.com"
grep -q 'security.ubuntu.com' "$S/apt/sources.list" || fail "the security source was rewritten"
grep -rq azure "$S/apt/sources.list" "$S/apt/sources.list.d/ubuntu.sources" && fail "an Azure source is left"
grep -qx 'update' "$S/log" || fail "install did not refresh the package lists after switching"
grep -q 'apt-retry: apt-get install failed' "$S/out" || fail "the switch was not logged"
grep -q 'Acquire::Retries' "$S/apt/apt.conf.d/80-sf-retries" || fail "no retry settings left for later apt-get calls"

reset_sources
run azure update || fail "update did not survive the Azure mirror"

reset_sources
run down update && fail "a mirror that is down everywhere reported success"
grep -q 'mirror+http://mirrors.ubuntu.com/mirrors.txt' "$S/apt/sources.list" || fail "the last fallback mirror was never tried"

reset_sources
run missing install -y nosuchpkg && fail "a missing package reported success"
[ "$(grep -c . "$S/log")" = 1 ] || { cat "$S/log"; fail "a missing package went through the other mirrors"; }
echo "apt_retry OK"
