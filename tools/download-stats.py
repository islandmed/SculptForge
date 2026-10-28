#!/usr/bin/env python3
"""Copy GitHub release downloads into GoatCounter, so the dashboard shows every download, not only clicks on the
website's buttons (most people download straight from GitHub, Reddit links or package managers).

GitHub keeps only an all-time total per release file. This compares the totals with the ones saved last run and
sends each new download to GoatCounter as an event "github download: <file> (<version>)", next to the website's
"download: <file>" click events. Run hourly by .github/workflows/download-stats.yml; the totals live in a JSON file
on the download-stats branch.

    GOATCOUNTER_KEY=... python3 tools/download-stats.py --state counts.json [--dry-run] [--backfill]

--backfill (first run only, no saved totals): sends every download so far, dated at its release's publish time.
Without it the first run only saves the totals.
"""
import argparse
import json
import os
import sys
import urllib.request
from datetime import datetime, timezone

REPO = "islandmed/SculptForge"
GOATCOUNTER = "https://sculptforge.goatcounter.com/api/v0/count"
SKIP = {"SHA256SUMS.txt"}  # checksums are not a download of the app


def api(url, token=None, data=None):
    headers = {"Accept": "application/json", "User-Agent": "sculptforge-download-stats"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if data is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(data).encode()
    with urllib.request.urlopen(urllib.request.Request(url, data=data, headers=headers), timeout=60) as r:
        return json.load(r) if r.status != 202 else None


def release_totals(github_token):
    """{"<tag>/<file>": {"count", "version", "file", "published"}} for every release file."""
    totals, page = {}, 1
    while True:
        releases = api(f"https://api.github.com/repos/{REPO}/releases?per_page=100&page={page}", github_token)
        if not releases:
            return totals
        for rel in releases:
            if rel["draft"]:
                continue
            for asset in rel["assets"]:
                if asset["name"] in SKIP:
                    continue
                totals[f"{rel['tag_name']}/{asset['name']}"] = {
                    "count": asset["download_count"],
                    "version": rel["tag_name"].lstrip("v"),
                    "file": asset["name"],
                    "published": rel["published_at"],
                }
        page += 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--state", required=True)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--backfill", action="store_true")
    args = ap.parse_args()

    totals = release_totals(os.environ.get("GITHUB_TOKEN"))
    first_run = not os.path.exists(args.state)
    saved = {} if first_run else json.load(open(args.state))

    now = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    hits = []
    for key, t in sorted(totals.items()):
        new = t["count"] - saved.get(key, 0)
        if first_run and not args.backfill:
            new = 0
        when = t["published"] if first_run else now
        hits += [{
            "path": f"github download: {t['file']} ({t['version']})",
            "title": f"Downloaded from GitHub, SculptForge {t['version']}",
            "event": True,
            "created_at": when,
        }] * max(new, 0)
        if new > 0:
            print(f"{new:4d}  {key}")
    print(f"{len(hits)} new downloads" + (" (first run)" if first_run else ""))

    if args.dry_run:
        return
    key = os.environ.get("GOATCOUNTER_KEY")
    if hits and not key:
        sys.exit("GOATCOUNTER_KEY is not set")
    for i in range(0, len(hits), 100):  # the count API takes at most 100 hits per call
        api(GOATCOUNTER, key, {"no_sessions": True, "hits": hits[i:i + 100]})
    with open(args.state, "w") as f:  # saved only after GoatCounter took every hit
        json.dump({k: t["count"] for k, t in totals.items()}, f, indent=1, sort_keys=True)
        f.write("\n")


if __name__ == "__main__":
    main()
