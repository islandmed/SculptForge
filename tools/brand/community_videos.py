#!/usr/bin/env python3
"""Rewrites the "Community videos" row of website/index.html from website/community-videos.json.
Click-to-play: the page loads only the local thumbnail; the youtube-nocookie player is created on click (script at the
end of index.html). Run from the repo root: python3 tools/brand/community_videos.py"""
import html, json, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PAGE = ROOT / "website/index.html"
DATA = ROOT / "website/community-videos.json"
START, END = "<!-- community-videos:start -->", "<!-- community-videos:end -->"
PLAY = '<svg viewBox="0 0 24 24" width="16" height="16" aria-hidden="true"><path d="M8 5.5v13a1 1 0 0 0 1.5.86l10.5-6.5a1 1 0 0 0 0-1.72L9.5 4.64A1 1 0 0 0 8 5.5z" fill="currentColor"/></svg>'


def note(v):
    e = html.escape
    if not v.get("since"):
        return ""
    items = "".join(f'<li>{e(i["text"])} <a href="{e(i["href"])}">Details</a></li>' for i in v["since"])
    still = v.get("still")
    tail = ""
    if still:
        t = e(still["text"]).replace(e(still["link"]), f'<a href="{e(still["href"])}">{e(still["link"])}</a>')
        tail = f"<p>{t}</p>"
    return f'<details class="cv-note"><summary>{e(v["since_label"])}</summary><ul>{items}</ul>{tail}</details>'


def card(v):
    e = html.escape
    for k in ("id", "caption", "by", "channel", "thumb"):
        if not v.get(k):
            sys.exit(f"community-videos.json: entry missing {k}: {v}")
    if not re.fullmatch(r"[A-Za-z0-9_-]{11}", v["id"]):
        sys.exit(f"community-videos.json: bad video id {v['id']}")
    if not (ROOT / "website" / v["thumb"]).exists():
        sys.exit(f"community-videos.json: no thumbnail website/{v['thumb']}")
    watch = f"https://www.youtube.com/watch?v={v['id']}"
    return f'''          <article class="cv-card" data-embed="https://www.youtube-nocookie.com/embed/{v['id']}?rel=0&amp;autoplay=1" data-title="{e(v['caption'])}">
            <div class="cv-media">
              <img src="{e(v['thumb'])}" alt="" width="1280" height="720" loading="lazy" decoding="async">
              <button type="button" class="cv-play" aria-label="Play: {e(v['caption'])}"><span>{PLAY}Play</span></button>
            </div>
            <div class="cv-body">
              <p class="cv-caption">{e(v['caption'])}</p>
              <p class="cv-by">By <a href="{e(v['channel'])}" rel="noopener">{e(v['by'])}</a> <span aria-hidden="true">&middot;</span> <a href="{watch}" rel="noopener">Watch on YouTube</a></p>
              {note(v)}
            </div>
          </article>'''


def main():
    vids = json.loads(DATA.read_text())["videos"]
    body = f'''{START}
    <section id="community-videos" class="section cv" aria-labelledby="cv-title">
      <div class="wrap">
        <p class="eyebrow">Community videos</p>
        <h2 id="cv-title">From the community</h2>
        <p class="section-lede">Walkthroughs and experiments from artists using SculptForge. A video loads from YouTube only when you press play.</p>
        <div class="cv-grid">
{chr(10).join(card(v) for v in vids)}
        </div>
      </div>
    </section>
    {END}'''
    s = PAGE.read_text()
    if START in s:
        s = re.sub(re.escape(START) + r".*?" + re.escape(END), lambda m: body, s, flags=re.S)
    else:
        anchor = '    <a class="platform-strip"'
        assert s.count(anchor) == 1, "anchor not found"
        s = s.replace(anchor, body + "\n\n" + anchor)
    PAGE.write_text(s)
    print(f"community videos: {len(vids)} in website/index.html")


main()
