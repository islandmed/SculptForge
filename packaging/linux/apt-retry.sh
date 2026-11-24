#!/bin/sh
# apt-get that survives a flaky mirror: apt-retry.sh update | install -y PKG... (any apt-get arguments; run as root, or it sudo's).
# 0.12.6: azure.archive.ubuntu.com failed DNS on the runners and every job that installs packages went red.
#   1. apt itself retries (Acquire::Retries) with short timeouts, IPv4 only; the same settings stay in apt.conf.d for later apt-get calls.
#   2. A failed run switches the Ubuntu sources to archive.ubuntu.com, then to mirror+http://mirrors.ubuntu.com/mirrors.txt, and runs again
#      (an install refreshes the package lists first). Each switch is logged.
#   3. A package that doesn't exist fails at once; nothing else is hidden: after the last mirror the real apt error is the exit status.
set -u
[ "$(id -u)" -eq 0 ] || [ -n "${APT_RETRY_ETC:-}" ] || exec sudo "$0" "$@"   # (APT_RETRY_ETC / APT_RETRY_APT_GET: tests/apt_retry_test.sh)
etc=${APT_RETRY_ETC:-/etc/apt}
[ $# -gt 0 ] || { echo "usage: apt-retry.sh update|install -y PKG..." >&2; exit 2; }
conf=$etc/apt.conf.d/80-sf-retries
mkdir -p "$etc/apt.conf.d"
[ -f "$conf" ] || printf 'Acquire::Retries "5";\nAcquire::http::Timeout "30";\nAcquire::https::Timeout "30";\nAcquire::ForceIPv4 "true";\nDpkg::Lock::Timeout "120";\n' > "$conf"
export DEBIAN_FRONTEND=noninteractive
log=$(mktemp)
trap 'rm -f "$log"' EXIT
run() {
  if command -v timeout >/dev/null 2>&1; then timeout "${APT_RETRY_TIMEOUT:-900}" "${APT_RETRY_APT_GET:-apt-get}" "$@" >"$log" 2>&1; else "${APT_RETRY_APT_GET:-apt-get}" "$@" >"$log" 2>&1; fi
  rc=$?; cat "$log"; return $rc
}
# switch_mirror BASE: point every archive.ubuntu.com / mirrors.ubuntu.com source at BASE; false when nothing changed.
switch_mirror() {
  changed=1
  for f in "$etc/sources.list" "$etc"/sources.list.d/*.list "$etc"/sources.list.d/*.sources; do
    [ -f "$f" ] || continue
    before=$(cksum < "$f")
    sed -i -E "s#(mirror\+)?https?://[A-Za-z0-9.-]*(archive|mirrors)\.ubuntu\.com/[^ ]*#$1#g" "$f"
    [ "$(cksum < "$f")" = "$before" ] || changed=0
  done
  return $changed
}
for mirror in "" "http://archive.ubuntu.com/ubuntu/" "mirror+http://mirrors.ubuntu.com/mirrors.txt"; do
  if [ -n "$mirror" ]; then
    switch_mirror "$mirror" || continue
    echo "apt-retry: apt-get $1 failed; retrying through $mirror" >&2
    if [ "$1" != update ]; then run update || continue; fi
  fi
  run "$@" && exit 0
  grep -Eq 'Unable to locate package|has no installation candidate|dpkg: error|E: Unmet dependencies' "$log" && break
  echo "apt-retry: apt-get $1 failed${mirror:+ on $mirror}" >&2
  sleep "${APT_RETRY_PAUSE:-5}"
done
echo "apt-retry: apt-get $* failed on every mirror" >&2
exit 1
