#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The home page says the newest release's news once (the announcement bar), however the generator runs.

    tools/guide/home_news_test.py

Checks the committed website/index.html and a synthetic page built from the generator's own functions: the newest
tagline appears at most once, the hero's news line is empty, the What's new lede carries no tagline, and
two-key shortcuts are not named on the home cards.
"""
import importlib.util
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("build_site", HERE / "build_site.py")
bs = importlib.util.module_from_spec(spec)
sys.modules["build_site"] = bs
spec.loader.exec_module(bs)


def plain(s):
    return re.sub(r"\s+", " ", re.sub(r"<[^>]+>", "", s)).lower()


def count(page_html, tagline):
    return plain(page_html).count(plain(bs.inline(tagline.rstrip("."), [])))


def check_page(page_html, wn, label, problems):
    tag = wn.versions[0]["tagline"]
    n = count(page_html, tag)
    if n > 1:
        problems.append(f"{label}: the tagline of {wn.versions[0]['version']} appears {n} times ('{tag}')")
    hero = re.search(r"<!-- hero-news:start -->(.*?)<!-- hero-news:end -->", page_html, re.S)
    if hero and hero.group(1).strip():
        problems.append(f"{label}: the hero still carries a news line: {hero.group(1).strip()[:80]}")
    lede = re.search(r'<p class="section-lede">(.*?)</p>\s*<div class="wn-home">', page_html, re.S)
    if lede and tag.rstrip(".")[:20].lower() in plain(lede.group(1)):
        problems.append(f"{label}: the What's new lede repeats the tagline")
    home = re.search(r"<!-- whats-new:start.*?<!-- whats-new:end -->", page_html, re.S)
    if home and re.search(r"two[- ]keys?\b|keys? in a row", plain(home.group(0))):
        problems.append(f"{label}: the What's new cards name two-key shortcuts")


SAMPLE = """# What's new

## Version 2.0.0

```features
date: 2026-01-02
tagline: Fast orbit, plus exact mirroring and cleaner cuts.
highlight: Fast orbit: a slower turn
highlight: Two-key shortcuts: A then S
```

- **Fast orbit** in Preferences.

## Version 1.9.0

```features
date: 2026-01-01
tagline: Reference images; two-key shortcuts; a start-up fix.
highlight: Reference images: pictures in the view
highlight: Two-key shortcuts: bind two keys in a row
```

- **Reference images** in the view.
"""

PAGE = """<body>
    <!-- news-hero:start -->
    <aside class="news-bar" data-version="1.9.0"><span class="news-bar-text">Reference images</span></aside>
    <!-- news-hero:end -->
          <!-- hero-news:start -->
          <p class="hero-news" data-version="1.9.0">New in 1.9.0: reference images.</p>
          <!-- hero-news:end -->
        <!-- whats-new:start (x) -->
        <p class="section-lede">old</p>
        <!-- whats-new:end -->
</body>"""


def main():
    problems = []
    index = (HERE.parent.parent / "website" / "index.html").read_text(encoding="utf-8")
    real = bs.WhatsNewPage(bs.WHATS_NEW, (bs.SRC / bs.WHATS_NEW).read_text(encoding="utf-8"))
    check_page(index, real, "website/index.html", problems)

    bs.project_version = lambda: "2.0.0"
    wn = bs.WhatsNewPage(bs.WHATS_NEW, SAMPLE)
    a = PAGE.index("\n", PAGE.index(bs.HOME_START)) + 1
    b = PAGE.index(bs.HOME_END)
    built = bs.write_news_hero(PAGE[:a] + bs.home_whats_new(wn) + "\n        " + PAGE[b:], wn)
    check_page(built, wn, "synthetic page", problems)
    if bs.inline(wn.versions[0]["tagline"].rstrip("."), []) not in built:
        problems.append("synthetic page: the bar lost the newest tagline")
    if bs.write_news_hero(built, wn) != built:
        problems.append("synthetic page: a second build changed it")
    for p in problems:
        print(p, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
