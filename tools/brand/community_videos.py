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
PLAY = ('<svg viewBox="0 0 68 48" width="68" height="48" aria-hidden="true"><path d="M66.5 7.7a8.5 8.5 0 0 0-6-6C55.2.3 34 .3 34 .3S12.8.3 '
        '7.5 1.7a8.5 8.5 0 0 0-6 6C0 13 0 24 0 24s0 11 1.5 16.3a8.5 8.5 0 0 0 6 6C12.8 47.7 34 47.7 34 47.7s21.2 0 26.5-1.4a8.5 8.5 0 0 0 6-6C68 35 '
        '68 24 68 24s0-11-1.5-16.3z" fill="#f28c3c"/><path d="M27 34V14l18 10z" fill="#1a0d04"/></svg>')


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
    return f'<div class="cv-note"><p class="cv-note-h">{e(v["since_label"])}</p><ul>{items}</ul>{tail}</div>'


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
    return f'''          <figure class="cv-card" data-embed="https://www.youtube-nocookie.com/embed/{v['id']}?rel=0&amp;autoplay=1" data-title="{e(v['caption'])}">
            <div class="hero-yt-box">
              <img src="{e(v['thumb'])}" alt="" width="1280" height="720" loading="lazy">
              <button type="button" class="hero-yt-play" aria-label="Play: {e(v['caption'])}">{PLAY}</button>
            </div>
            <noscript><p class="cv-credit"><a href="{watch}" rel="noopener">Watch on YouTube</a></p></noscript>
            <figcaption><a class="cv-title" href="{watch}" rel="noopener">{e(v['caption'])}</a>
              <span class="cv-by">by <a href="{e(v['channel'])}" rel="noopener">{e(v['by'])}</a></span></figcaption>
            {note(v)}
          </figure>'''


def main():
    vids = json.loads(DATA.read_text())["videos"]
    body = f'''{START}
    <section id="community-videos" class="section cv" aria-labelledby="cv-title">
      <div class="wrap">
        <p class="eyebrow">Community videos</p>
        <h2 id="cv-title">SculptForge, shown by the people using it</h2>
        <p class="section-lede">Made and shared by artists in the community. A video loads from YouTube only when you press play.</p>
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
