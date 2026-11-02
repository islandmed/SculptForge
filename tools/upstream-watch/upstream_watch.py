#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Watches the upstream projects SculptForge targets and opens a GitHub issue when one has a newer release.

    upstream_watch.py check                 print each item's targeted and latest version (no token needed)
    upstream_watch.py run --repo OWNER/NAME open one issue per newer item and update the monthly digest
                     [--dry-run]            print what it would do instead

The targeted versions live in packaging/upstream.json; bump an item there when the issue's checklist is done.
Only public JSON APIs are read (GitHub REST, endoflife.date); Unreal Engine has none, so only the <title> of its
release notes page is read. Issues get the "upstream" label and are deduplicated by title; an older open issue
for the same item is closed as superseded. "Upstream digest YYYY-MM" collects everything behind that month.
Stdlib only, so the workflow needs no installs.
"""
import argparse
import datetime
import json
import os
import pathlib
import re
import sys
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_CONFIG = ROOT / "packaging" / "upstream.json"
LABEL = "upstream"
API = "https://api.github.com"
DEFAULT_TAG = r"^v?(\d+(?:[._]\d+)*)[a-z]?$"
UA = "sculptforge-upstream-watch"


def parse_version(text):
    """'v3_6_1' / 'release-3.2.30' / '25.08' -> (3, 6, 1) etc.; None when there are no digits."""
    parts = re.findall(r"\d+", text or "")
    return tuple(int(p) for p in parts) if parts else None


def truncate(version, track):
    """Keeps the components an item is tracked at: 'minor' -> 2, 'major' -> 1, 'patch' (default) -> all."""
    n = {"major": 1, "minor": 2}.get(track)
    return version[:n] if n else version


def fmt(version):
    return ".".join(str(p) for p in version)


def is_newer(latest, targeted, track=None):
    """True when latest is past targeted at the item's tracking granularity (missing parts count as 0)."""
    a, b = truncate(latest, track), truncate(targeted, track)
    width = max(len(a), len(b))
    return a + (0,) * (width - len(a)) > b + (0,) * (width - len(b))


class Http:
    """GET/POST/PATCH JSON. Tests replace it with a recorded one."""

    def __init__(self, token=None):
        self.token = token

    def request(self, method, url, body=None, accept="application/json"):
        headers = {"User-Agent": UA, "Accept": accept}
        if self.token and url.startswith(API):
            headers["Authorization"] = f"Bearer {self.token}"
            headers["X-GitHub-Api-Version"] = "2022-11-28"
        data = None
        if body is not None:
            data = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(url, data=data, headers=headers, method=method)
        with urllib.request.urlopen(req, timeout=30) as r:
            raw = r.read().decode("utf-8", "replace")
        if "json" in accept:
            return json.loads(raw) if raw else None
        return raw

    def get(self, url):
        return self.request("GET", url)

    def get_text(self, url):
        return self.request("GET", url, accept="text/html")

    def post(self, url, body):
        return self.request("POST", url, body)

    def patch(self, url, body):
        return self.request("PATCH", url, body)


# --- latest-version sources: each returns (version tuple, release-notes link) or raises ---

def latest_github(src, http):
    pat = re.compile(src.get("tag", DEFAULT_TAG))
    best = None
    for rel in http.get(f"{API}/repos/{src['repo']}/releases?per_page=30") or []:
        if rel.get("draft") or (rel.get("prerelease") and not src.get("prerelease")):
            continue
        m = pat.match(rel.get("tag_name", ""))
        v = parse_version(m.group(1)) if m else None
        if v and (best is None or v > best[0]):
            best = (v, rel.get("html_url") or f"https://github.com/{src['repo']}/releases")
    if not best:
        raise ValueError(f"no release of {src['repo']} matches {pat.pattern}")
    return best


def latest_endoflife(src, http):
    best = None
    for cycle in http.get(f"https://endoflife.date/api/{src['product']}.json") or []:
        v = parse_version(str(cycle.get("latest", "")))
        if v and (best is None or v > best):
            best = v
    if not best:
        raise ValueError(f"endoflife.date has no releases of {src['product']}")
    return best, None


def latest_github_dir(src, http):
    """Newest file name in a repo directory, e.g. the VFX Reference Platform's _data/platforms/CY2027.yml."""
    pat = re.compile(src["name"])
    best = None
    for entry in http.get(f"{API}/repos/{src['repo']}/contents/{src['path']}") or []:
        m = pat.match(entry.get("name", ""))
        v = parse_version(m.group(1)) if m else None
        if v and (best is None or v > best):
            best = v
    if not best:
        raise ValueError(f"nothing in {src['repo']}/{src['path']} matches {pat.pattern}")
    return best, None


def latest_page_title(src, http):
    """Unreal Engine has no public API: its release notes URL opens the newest version, named in the <title>."""
    html = http.get_text(src["url"])
    m = re.search(r"<title>[^<]*?" + src["title"], html or "", re.IGNORECASE)
    if not m:
        raise ValueError("release notes title not found (the page may be behind a bot check)")
    return parse_version(m.group(1)), None


SOURCES = {"github": latest_github, "endoflife": latest_endoflife, "github_dir": latest_github_dir,
           "page_title": latest_page_title}


def notes_link(item, version, found):
    tmpl = item.get("notes")
    if tmpl:
        return tmpl.format(version=fmt(version), major=version[0], minor=version[1] if len(version) > 1 else 0,
                           nodots="".join(str(p) for p in version))
    return found or ""


def survey(config, http):
    """Returns (behind, failed): items with a newer release, and items whose source couldn't be read."""
    behind, failed = [], []
    for item in config["items"]:
        src = item["source"]
        try:
            latest, found = SOURCES[src["kind"]](src, http)
        except (urllib.error.URLError, ValueError, KeyError, TypeError, OSError) as e:
            failed.append({"name": item["name"], "error": str(e)})
            continue
        targeted = parse_version(item["version"])
        if is_newer(latest, targeted, item.get("track")):
            shown = truncate(latest, item.get("track"))
            behind.append({"name": item["name"], "targeted": item["version"], "latest": fmt(shown),
                           "link": notes_link(item, shown, found), "why": item.get("why", "")})
    return behind, failed


# --- issues ---

def issue_title(entry):
    return f"Upstream: {entry['name']} {entry['latest']}"


def digest_title(today):
    return f"Upstream digest {today:%Y-%m}"


def issue_body(entry):
    why = f"\n{entry['why']}\n" if entry["why"] else ""
    link = entry["link"] or "(see the project's site)"
    return (f"**{entry['name']} {entry['latest']}** is out; `packaging/upstream.json` targets {entry['targeted']}.\n"
            f"{why}\nRelease notes: {link}\n\n"
            f"- [ ] Bump (dependency, CI matrix or docs that name the version; then `packaging/upstream.json`)\n"
            f"- [ ] Test (full ctest; the integration it affects, if any)\n"
            f"- [ ] Notes (CHANGELOG / What's New line if users see a difference)\n\n"
            f"_Opened by .github/workflows/upstream-watch.yml._")


def digest_body(behind, failed, numbers, today):
    lines = [f"What changed upstream as of {today:%Y-%m-%d} (updated by every weekly run this month).", ""]
    if behind:
        lines += ["| Project | We target | Latest | Issue |", "|---|---|---|---|"]
        for e in behind:
            num = numbers.get(issue_title(e))
            latest = f"[{e['latest']}]({e['link']})" if e["link"] else e["latest"]
            lines.append(f"| {e['name']} | {e['targeted']} | {latest} | {f'#{num}' if num else ''} |")
    else:
        lines.append("Everything we track is current.")
    if failed:
        lines += ["", "Couldn't check: " + "; ".join(f"{f['name']} ({f['error']})" for f in failed)]
    return "\n".join(lines)


def list_issues(http, repo):
    """Every issue (open and closed, no PRs) with the upstream label."""
    out, page = [], 1
    while True:
        batch = http.get(f"{API}/repos/{repo}/issues?labels={LABEL}&state=all&per_page=100&page={page}") or []
        out += [i for i in batch if "pull_request" not in i]
        if len(batch) < 100:
            return out
        page += 1


def plan(behind, failed, existing, today):
    """Pure: what to do, given the survey and the existing upstream issues.

    Returns a list of actions: ("create", title, body), ("close", number, comment), ("update", number, body)."""
    by_title = {i["title"]: i for i in existing}
    actions, numbers = [], {t: i["number"] for t, i in by_title.items()}
    for e in behind:
        title = issue_title(e)
        if title in by_title:
            continue
        actions.append(("create", title, issue_body(e)))
        prefix = f"Upstream: {e['name']} "
        for i in existing:
            rest = i["title"][len(prefix):] if i["title"].startswith(prefix) else ""
            if i["state"] == "open" and rest and " " not in rest and i["title"] != title:
                actions.append(("close", i["number"], f"Superseded by {e['name']} {e['latest']}."))
    dtitle = digest_title(today)
    if dtitle in by_title:
        actions.append(("update", by_title[dtitle]["number"], None))
    elif behind:
        actions.append(("create", dtitle, None))
    return actions, numbers


def apply(actions, numbers, behind, failed, http, repo, today, out=print):
    """Runs the planned actions; the digest body is written last so it can link the issues just opened."""
    base = f"{API}/repos/{repo}"
    digest = digest_title(today)
    for kind, key, body in actions:
        if kind == "create" and key != digest:
            made = http.post(f"{base}/issues", {"title": key, "body": body, "labels": [LABEL]})
            numbers[key] = made["number"]
            out(f"opened #{made['number']} {key}")
        elif kind == "close":
            http.post(f"{base}/issues/{key}/comments", {"body": body})
            http.patch(f"{base}/issues/{key}", {"state": "closed", "state_reason": "not_planned"})
            out(f"closed #{key} ({body})")
    for kind, key, _ in actions:
        if kind == "create" and key == digest:
            made = http.post(f"{base}/issues", {"title": key, "labels": [LABEL],
                                                "body": digest_body(behind, failed, numbers, today)})
            out(f"opened #{made['number']} {key}")
        elif kind == "update":
            http.patch(f"{base}/issues/{key}", {"body": digest_body(behind, failed, numbers, today)})
            out(f"updated #{key} {digest}")


def ensure_label(http, repo):
    try:
        http.post(f"{API}/repos/{repo}/labels",
                  {"name": LABEL, "color": "0e8a16", "description": "A newer upstream release to adopt"})
    except urllib.error.HTTPError as e:
        if e.code != 422:  # 422: it already exists
            raise


def load_config(path):
    return json.loads(pathlib.Path(path).read_text())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("command", choices=("check", "run"))
    ap.add_argument("--config", default=DEFAULT_CONFIG)
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args(argv)
    config = load_config(args.config)
    http = Http(os.environ.get("GITHUB_TOKEN"))
    today = datetime.date.today()
    behind, failed = survey(config, http)
    if args.command == "check":
        names = {e["name"]: e for e in behind}
        for item in config["items"]:
            e = names.get(item["name"])
            print(f"{item['name']:22} {item['version']:10} {'-> ' + e['latest'] if e else 'current'}")
        for f in failed:
            print(f"{f['name']:22} could not check: {f['error']}")
        for name, why in config.get("skipped", {}).items():
            print(f"{name:22} skipped: {why}")
        return 0
    if not args.repo:
        ap.error("--repo or GITHUB_REPOSITORY is needed")
    existing = list_issues(http, args.repo)
    actions, numbers = plan(behind, failed, existing, today)
    if args.dry_run:
        for kind, key, _ in actions:
            print(f"would {kind} {key}")
        return 0
    if actions:
        ensure_label(http, args.repo)
    apply(actions, numbers, behind, failed, http, args.repo, today)
    for f in failed:
        print(f"::warning::{f['name']}: {f['error']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
