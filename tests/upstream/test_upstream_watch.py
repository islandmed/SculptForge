#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/upstream-watch against recorded API responses (tests/data/upstream): version comparison, each source,
issue dedupe by title, superseded issues, the monthly digest, and that packaging/upstream.json is well formed."""
import datetime
import json
import pathlib
import sys
import unittest
import urllib.error

ROOT = pathlib.Path(__file__).resolve().parents[2]
DATA = ROOT / "tests" / "data" / "upstream"
sys.path.insert(0, str(ROOT / "tools" / "upstream-watch"))
import upstream_watch as uw

TODAY = datetime.date(2026, 11, 2)
REPO = "example/sculptforge"


class Recorded:
    """Serves recorded responses by URL substring and records every write."""

    def __init__(self, routes, issues=()):
        self.routes, self.issues, self.writes, self.next = routes, list(issues), [], 100

    def _find(self, url):
        for key, value in self.routes.items():
            if key in url:
                if isinstance(value, Exception):
                    raise value
                return (DATA / value).read_text() if isinstance(value, str) else value
        raise urllib.error.URLError(f"no recording for {url}")

    def get(self, url):
        if "/issues?" in url:
            return self.issues if "page=1" in url else []
        return json.loads(self._find(url))

    def get_text(self, url):
        return self._find(url)

    def post(self, url, body):
        self.writes.append(("POST", url, body))
        if url.endswith("/issues"):
            self.next += 1
            return {"number": self.next}
        return {}

    def patch(self, url, body):
        self.writes.append(("PATCH", url, body))
        return {}


ROUTES = {"libsdl-org/SDL": "sdl_releases.json", "OpenTimelineIO": "otio_releases.json",
          "OpenSubdiv": "opensubdiv_releases.json", "endoflife.date/api/python": "python_eol.json",
          "ves-tech/vfxplatform": "vfx_platforms.json", "dev.epicgames.com": "unreal_notes.html"}

ITEMS = [
    {"name": "SDL3", "version": "3.2", "track": "minor",
     "source": {"kind": "github", "repo": "libsdl-org/SDL", "tag": r"^release-(3\.\d+\.\d+)$"}},
    {"name": "OpenTimelineIO", "version": "0.18.1",
     "source": {"kind": "github", "repo": "AcademySoftwareFoundation/OpenTimelineIO", "prerelease": True}},
    {"name": "OpenSubdiv", "version": "3.7.0", "source": {"kind": "github", "repo": "PixarAnimationStudios/OpenSubdiv"}},
    {"name": "Python", "version": "3.13", "track": "minor", "source": {"kind": "endoflife", "product": "python"},
     "notes": "https://docs.python.org/{major}.{minor}/whatsnew/{major}.{minor}.html"},
    {"name": "VFX Reference Platform", "version": "CY2026", "notes": "https://vfxplatform.com/",
     "source": {"kind": "github_dir", "repo": "ves-tech/vfxplatform", "path": "_data/platforms", "name": r"^CY(\d{4})\.yml$"}},
    {"name": "Unreal Engine", "version": "5.8", "track": "minor",
     "source": {"kind": "page_title", "url": "https://dev.epicgames.com/x", "title": r"Unreal Engine (\d+\.\d+) Release Notes"}},
]


class Versions(unittest.TestCase):
    def test_parse(self):
        self.assertEqual(uw.parse_version("v3_6_1"), (3, 6, 1))
        self.assertEqual(uw.parse_version("release-3.2.30"), (3, 2, 30))
        self.assertEqual(uw.parse_version("v25.08"), (25, 8))
        self.assertEqual(uw.parse_version("CY2027"), (2027,))
        self.assertIsNone(uw.parse_version("latest"))

    def test_compare(self):
        self.assertTrue(uw.is_newer((26, 11), (26, 8)))
        self.assertTrue(uw.is_newer((1, 93, 0), (1, 91, 9)))
        self.assertFalse(uw.is_newer((25, 5), (25, 5, 0)))      # missing parts count as 0
        self.assertTrue(uw.is_newer((25, 5, 1), (25, 5)))
        self.assertFalse(uw.is_newer((3, 2, 31), (3, 2), "minor"))  # patch releases don't count at minor
        self.assertTrue(uw.is_newer((3, 4, 0), (3, 2, 30), "minor"))
        self.assertFalse(uw.is_newer((3, 15, 0), (3, 13), "major"))
        self.assertFalse(uw.is_newer((1, 0), (1, 0, 2)))


class Sources(unittest.TestCase):
    def setUp(self):
        self.http = Recorded(ROUTES)

    def test_github_filters_tags_and_prereleases(self):
        v, link = uw.latest_github(ITEMS[0]["source"], self.http)
        self.assertEqual(v, (3, 4, 18))   # not SDL 2.x, not the 3.5 preview
        self.assertTrue(link.endswith("release-3.4.18"))

    def test_github_prerelease_opt_in(self):
        self.assertEqual(uw.latest_github(ITEMS[1]["source"], self.http)[0], (0, 18, 1))
        with self.assertRaises(ValueError):  # every OTIO release is marked prerelease
            uw.latest_github({"repo": "AcademySoftwareFoundation/OpenTimelineIO"}, self.http)

    def test_underscore_tags(self):
        self.assertEqual(uw.latest_github(ITEMS[2]["source"], self.http)[0], (3, 8, 0))

    def test_endoflife(self):
        self.assertEqual(uw.latest_endoflife(ITEMS[3]["source"], self.http)[0][:2], (3, 15))

    def test_vfx_platform(self):
        self.assertEqual(uw.latest_github_dir(ITEMS[4]["source"], self.http)[0], (2027,))

    def test_unreal_title_and_bot_check(self):
        self.assertEqual(uw.latest_page_title(ITEMS[5]["source"], self.http)[0], (5, 8))
        blocked = Recorded({"dev.epicgames.com": "unreal_challenge.html"})
        with self.assertRaises(ValueError):
            uw.latest_page_title(ITEMS[5]["source"], blocked)

    def test_survey_reports_failures_without_stopping(self):
        http = Recorded({**ROUTES, "OpenSubdiv": urllib.error.URLError("timed out")})
        behind, failed = uw.survey({"items": ITEMS}, http)
        self.assertEqual([e["name"] for e in behind], ["SDL3", "Python", "VFX Reference Platform"])
        self.assertEqual([f["name"] for f in failed], ["OpenSubdiv"])
        sdl, py, vfx = behind
        self.assertEqual(sdl["latest"], "3.4")
        self.assertEqual(py["link"], "https://docs.python.org/3.15/whatsnew/3.15.html")
        self.assertEqual(vfx["latest"], "2027")


class Issues(unittest.TestCase):
    def behind(self):
        return uw.survey({"items": ITEMS}, Recorded(ROUTES))[0]

    def test_new_items_open_issues_and_digest(self):
        http = Recorded(ROUTES)
        behind = self.behind()
        actions, numbers = uw.plan(behind, [], [], TODAY)
        uw.apply(actions, numbers, behind, [], http, REPO, TODAY, out=lambda *_: None)
        titles = [b["title"] for m, u, b in http.writes if m == "POST"]
        self.assertEqual(titles, ["Upstream: SDL3 3.4", "Upstream: OpenSubdiv 3.8.0", "Upstream: Python 3.15",
                                  "Upstream: VFX Reference Platform 2027", "Upstream digest 2026-11"])
        for _, _, body in http.writes:
            self.assertEqual(body["labels"], ["upstream"])
        first = http.writes[0][2]["body"]
        for part in ("3.4", "targets 3.2", "release-3.4.18", "- [ ] Bump", "- [ ] Test", "- [ ] Notes"):
            self.assertIn(part, first)
        digest = http.writes[-1][2]["body"]
        self.assertIn("| SDL3 | 3.2 |", digest)
        self.assertIn("#101", digest)  # links the issue opened in the same run

    def test_dedupe_by_title_open_or_closed(self):
        existing = [{"title": "Upstream: SDL3 3.4", "number": 7, "state": "closed"},
                    {"title": "Upstream: Python 3.15", "number": 8, "state": "open"},
                    {"title": "Upstream: OpenSubdiv 3.8.0", "number": 9, "state": "open"},
                    {"title": "Upstream: VFX Reference Platform 2027", "number": 10, "state": "open"},
                    {"title": "Upstream digest 2026-11", "number": 11, "state": "open"}]
        actions, numbers = uw.plan(self.behind(), [], existing, TODAY)
        self.assertEqual(actions, [("update", 11, None)])
        http = Recorded(ROUTES)
        uw.apply(actions, numbers, self.behind(), [], http, REPO, TODAY, out=lambda *_: None)
        self.assertEqual(len(http.writes), 1)
        self.assertIn("#7", http.writes[0][2]["body"])

    def test_superseded_issue_is_closed(self):
        existing = [{"title": "Upstream: Python 3.14", "number": 5, "state": "open"},
                    {"title": "Upstream: Python 3.12", "number": 4, "state": "closed"},
                    {"title": "Upstream: Python Tools 1.0", "number": 3, "state": "open"}]
        py = [e for e in self.behind() if e["name"] == "Python"]
        actions, _ = uw.plan(py, [], existing, TODAY)
        self.assertIn(("create", "Upstream: Python 3.15", uw.issue_body(py[0])), actions)
        self.assertIn(("close", 5, "Superseded by Python 3.15."), actions)
        self.assertNotIn(4, [a[1] for a in actions if a[0] == "close"])
        # "Python Tools" starts with "Upstream: Python " too: only a same-name issue is superseded.
        self.assertNotIn(3, [a[1] for a in actions if a[0] == "close"])

    def test_quiet_month_opens_nothing(self):
        actions, _ = uw.plan([], [{"name": "Unreal Engine", "error": "bot check"}], [], TODAY)
        self.assertEqual(actions, [])

    def test_digest_lists_unchecked_sources(self):
        body = uw.digest_body([], [{"name": "Unreal Engine", "error": "bot check"}], {}, TODAY)
        self.assertIn("Everything we track is current", body)
        self.assertIn("Unreal Engine (bot check)", body)


class Config(unittest.TestCase):
    def test_upstream_json(self):
        config = uw.load_config(uw.DEFAULT_CONFIG)
        names = [i["name"] for i in config["items"]]
        self.assertEqual(len(names), len(set(names)))
        for item in config["items"]:
            self.assertIn(item["source"]["kind"], uw.SOURCES, item["name"])
            self.assertIsNotNone(uw.parse_version(item["version"]), item["name"])
            self.assertIn(item.get("track", "patch"), ("patch", "minor", "major"), item["name"])
            if "notes" in item:
                uw.notes_link(item, (1, 2, 3), None)


if __name__ == "__main__":
    unittest.main()
