#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Shrinks a failing UI test script (a mode matrix or monkey replay, --ui-out/replay-*.sfui) to the fewest lines
that still fail the same way (delta debugging), so a fix task gets a short repro.

  tools/test/shrink_sfui.py APP REPLAY.sfui [--out SHORT.sfui] [--keep-head N] [--timeout S]

Lines before `snapshot_start` (the set-up) and from the last `release` on (the way out and the checks) are kept; the
user actions between are shrunk. "The same way" = the first FAIL's message, without line numbers and paths. Runs the
app offscreen with SF_NO_DIALOGS=1 (never a window or dialog on a desktop).
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile


def failure(app, lines, workdir, timeout):
    path = os.path.join(workdir, "try.sfui")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    env = dict(os.environ, SDL_VIDEO_DRIVER="offscreen", SF_NO_DIALOGS="1", SF_NO_UPDATE_CHECK="1")
    try:
        r = subprocess.run([app, "--ui-test", path, "--ui-out", os.path.join(workdir, "out")], env=env,
                           capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "timeout"
    for line in (r.stdout + r.stderr).splitlines():
        if "FAIL " in line and "Screenshot" not in line:
            msg = re.sub(r"\S+\.sfui:\d+: ", "", line.split("FAIL ", 1)[1])
            return re.sub(r"\s*\(replay: [^)]*\)", "", msg).strip()
    return None if r.returncode == 0 else f"exit {r.returncode}"


def split(lines):
    start = next((i + 1 for i, l in enumerate(lines) if l.strip() == "snapshot_start"), 0)
    end = max((i for i, l in enumerate(lines) if l.strip() == "release"), default=len(lines))
    if end < start:
        end = len(lines)
    return lines[:start], lines[start:end], lines[end:]


def ddmin(items, fails):
    n = 2
    while len(items) >= 2:
        chunk = max(1, len(items) // n)
        reduced = False
        for i in range(0, len(items), chunk):
            rest = items[:i] + items[i + chunk:]
            if fails(rest):
                items, n, reduced = rest, max(n - 1, 2), True
                break
        if not reduced:
            if n >= len(items):
                break
            n = min(len(items), n * 2)
    return items


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("app")
    ap.add_argument("replay")
    ap.add_argument("--out")
    ap.add_argument("--timeout", type=float, default=120)
    a = ap.parse_args()
    with open(a.replay, encoding="utf-8") as f:
        lines = [l.rstrip("\n") for l in f if l.strip() and not l.startswith("#")]
    head, body, tail = split(lines)
    with tempfile.TemporaryDirectory() as work:
        want = failure(a.app, lines, work, a.timeout)
        if not want:
            print("the replay passes: nothing to shrink")
            return 1
        print(f"failure: {want}\n{len(body)} action lines to shrink")
        runs = [0]

        def fails(candidate):
            runs[0] += 1
            return failure(a.app, head + candidate + tail, work, a.timeout) == want

        body = ddmin(body, fails)
    out = a.out or re.sub(r"\.sfui$", "", a.replay) + "-short.sfui"
    with open(out, "w", encoding="utf-8") as f:
        f.write(f"# shrunk from {os.path.basename(a.replay)} ({runs[0]} runs): {want}\n")
        f.write("\n".join(head + body + tail) + "\n")
    print(f"{len(body)} action lines left: {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
