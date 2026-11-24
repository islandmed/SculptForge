#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""How the free Hugging Face Spaces behind Generate 3D are doing, for the update feed (no Claude, no GPU quota).

Asks Hugging Face's public API for each Space's runtime stage and writes the result as the optional "ai3d" block of
website/latest.json, which the app already fetches once a day (src/ai/SpaceHealth.h reads it: a switched-off Space is
greyed out with the reason, a run moves to one that is up). Only Spaces that are NOT up are listed; no block = all up.

    scripts/ai3d-health.py            print what the block would be (and whether it differs from the file's)
    scripts/ai3d-health.py --write    rewrite website/latest.json's "ai3d" block when it changed (other keys untouched)

A Space the probe can't reach this time keeps its last state (a network blip is not an outage). Run by
scripts/release.sh at every release, and by sf-hfhealth.timer through the site-data lane. Exit 0 always.
"""
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

# Generate 3D preset id (src/ai/ImageTo3DService.cpp hfPresets) -> the Space's repository.
SPACES = {
    "hunyuan3d-2.1": "tencent/Hunyuan3D-2.1",
    "hunyuan3d-2-turbo": "tencent/Hunyuan3D-2mini-Turbo",
    "hunyuan3d-2": "tencent/Hunyuan3D-2",
    "trellis-2": "microsoft/TRELLIS.2",
    "stable-fast-3d": "stabilityai/stable-fast-3d",
}
OFF = {"PAUSED": "Switched off by its author", "RUNTIME_ERROR": "Not running: it has an error on Hugging Face",
       "BUILD_ERROR": "Not running: it failed to build", "CONFIG_ERROR": "Not running: its settings are broken",
       "NO_APP_FILE": "Not running: it has no app", "DELETED": "Gone from Hugging Face"}
SLOW = {"SLEEPING": "Asleep: the first run wakes it, which takes a minute or two", "STOPPED": "Stopped: the first run starts it",
        "BUILDING": "Rebuilding", "RUNNING_BUILDING": "Rebuilding", "APP_STARTING": "Starting up"}
FEED = Path("website/latest.json")


def probe(repo):
    """('up'|'slow'|'off', note) or None when Hugging Face can't be reached."""
    req = urllib.request.Request(f"https://huggingface.co/api/spaces/{repo}", headers={"User-Agent": "sculptforge-health/1"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            stage = json.load(r).get("runtime", {}).get("stage", "")
    except urllib.error.HTTPError as e:
        return ("off", "Gone from Hugging Face") if e.code == 404 else None
    except Exception:
        return None
    if stage == "RUNNING":
        return "up", ""
    if stage in OFF:
        return "off", OFF[stage]
    if stage in SLOW:
        return "slow", SLOW[stage]
    return None  # an unknown stage: say nothing rather than guess


def main():
    write = "--write" in sys.argv[1:]
    feed = json.loads(FEED.read_text(encoding="utf-8")) if FEED.exists() else {}
    last = feed.get("ai3d", {}).get("spaces", {})
    spaces = {}
    for pid, repo in SPACES.items():
        got = probe(repo)
        if got is None:
            if pid in last:
                spaces[pid] = last[pid]
            continue
        if got[0] != "up":
            spaces[pid] = {"state": got[0], "note": got[1]}
    block = {"spaces": spaces} if spaces else None
    changed = block != feed.get("ai3d")
    print(json.dumps(block, indent=2) if block else "all Spaces up", "(changed)" if changed else "(same as the feed)")
    if write and changed:
        if block:
            feed["ai3d"] = block
        else:
            feed.pop("ai3d", None)
        tmp = FEED.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(feed, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        tmp.replace(FEED)


if __name__ == "__main__":
    main()
