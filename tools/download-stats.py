#!/usr/bin/env python3
"""Keep a per-day history of GitHub release downloads for the stats page (website/stats/).

GitHub keeps only an all-time total per release file. Run hourly by .github/workflows/download-stats.yml, this
compares the totals with the ones saved last run and adds the difference to today's entry (Pacific time) in
history.json, which lives on the download-stats branch next to counts.json (the last totals).

    python3 tools/download-stats.py --dir <folder with counts.json / history.json> [--dry-run]

history.json:
    {"updated": "<UTC time>", "tracking_since": "<UTC time>",
     "before": {"<tag>/<file>": n},          downloads before tracking began, by file (dates unknown)
     "days": {"YYYY-MM-DD": {"<tag>/<file>": n}}}
When history.json is first created, downloads of releases published today are counted as today's (they can only
have happened today); everything older goes to "before".
"""
import argparse
import json
import os
import urllib.error
import urllib.request
from datetime import datetime, timezone
from zoneinfo import ZoneInfo

REPO = "islandmed/SculptForge"
ZONE = ZoneInfo("America/Vancouver")
SKIP = {"SHA256SUMS.txt"}  # checksums are not a download of the app


def get(url, token=None):
    headers = {"Accept": "application/vnd.github+json", "User-Agent": "sculptforge-download-stats"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        raise SystemExit(f"{url}: HTTP {e.code}: {e.read().decode(errors='replace')[:500]}")


def release_files(token):
    """{"<tag>/<file>": (count, published_at)} for every published release file."""
    files, page = {}, 1
    while releases := get(f"https://api.github.com/repos/{REPO}/releases?per_page=100&page={page}", token):
        for rel in releases:
            if rel["draft"]:
                continue
            for asset in rel["assets"]:
                if asset["name"] not in SKIP:
                    files[f"{rel['tag_name']}/{asset['name']}"] = (asset["download_count"], rel["published_at"])
        page += 1
    return files


def load(path, default):
    return json.load(open(path)) if os.path.exists(path) else default


def save(path, value):
    with open(path, "w") as f:
        json.dump(value, f, indent=1, sort_keys=True)
        f.write("\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", required=True)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    counts_path, history_path = (os.path.join(args.dir, n) for n in ("counts.json", "history.json"))

    files = release_files(os.environ.get("GITHUB_TOKEN"))
    now = datetime.now(timezone.utc)
    stamp = now.replace(microsecond=0).isoformat().replace("+00:00", "Z")
    today = now.astimezone(ZONE).date().isoformat()
    saved = load(counts_path, {})
    history = load(history_path, None)

    if history is None:  # first run: split what's there into "before" and today
        history = {"tracking_since": stamp, "before": {}, "days": {}}
        for key, (count, published) in files.items():
            day = datetime.fromisoformat(published.replace("Z", "+00:00")).astimezone(ZONE).date().isoformat()
            bucket = history["days"].setdefault(today, {}) if day == today else history["before"]
            if count:
                bucket[key] = count
        saved = {key: count for key, (count, _) in files.items()}
        print(f"history started: {sum(history['days'].get(today, {}).values())} today, "
              f"{sum(history['before'].values())} before")

    added = 0
    for key, (count, _) in sorted(files.items()):
        new = count - saved.get(key, 0)
        if new > 0:
            day = history["days"].setdefault(today, {})
            day[key] = day.get(key, 0) + new
            added += new
            print(f"{new:4d}  {key}")
    history["updated"] = stamp
    print(f"{added} new downloads; today {sum(history['days'].get(today, {}).values())}")

    if not args.dry_run:
        save(history_path, history)
        save(counts_path, {key: count for key, (count, _) in files.items()})


if __name__ == "__main__":
    main()
