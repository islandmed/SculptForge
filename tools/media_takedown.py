#!/usr/bin/env python3
"""Takes every image and video that shows a mark off a site folder before it deploys (pages.yml).

A file whose sha256 tools/marks_media_flagged.txt lists is deleted, and every
HTML page in the folder loses its references to it: the <figure> or <a> around it, the <img>, the <source> (and
the <video> when none is left, or just its clean poster as a still), a poster attribute. Works on any copy of the site, including a release tag's guide.

  media_takedown.py website            take them down (prints each file and page touched)
  media_takedown.py --self-test
"""
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FLAGGED = ROOT / "tools" / "marks_media_flagged.txt"
MEDIA = (".png", ".jpg", ".jpeg", ".webp", ".avif", ".gif", ".mp4", ".webm", ".mov", ".m4v")
ATTR = re.compile(r'\b(?:src|href|poster|content)="([^"]+)"')


def flagged_hashes():
    return {line.strip() for line in FLAGGED.read_text().splitlines() if line.strip() and not line.startswith("#")}


def target(page, value, site):
    """The file in the site an attribute value points at, or None."""
    value = value.split("?", 1)[0].split("#", 1)[0]
    if not value or ":" in value.split("/", 1)[0]:
        prefix = "https://sculptforge.app/"
        if not value.startswith(prefix):
            return None
        return (site / value[len(prefix):]).resolve()
    base = site if value.startswith("/") else page.parent
    return (base / value.lstrip("/")).resolve()


def strip(html, page, site, gone):
    def bad(fragment):
        return any(target(page, v, site) in gone for v in ATTR.findall(fragment))


    def video(m):
        if not bad(m.group(0)):
            return m.group(0)
        block = re.sub(r'\s*\bposter="([^"]+)"', lambda p: "" if bad(p.group(0)) else p.group(0), m.group(0))
        block = re.sub(r"\s*<source\b[^>]*>", lambda s: "" if bad(s.group(0)) else s.group(0), block)
        opening = re.match(r"<video\b[^>]*>", block).group(0)
        if bad(re.sub(r'\bposter="[^"]*"', "", opening)):
            block = block.replace(opening, re.sub(r'\s*\bsrc="[^"]*"', "", opening))
            opening = re.match(r"<video\b[^>]*>", block).group(0)
        if "src=" not in opening and "<source" not in block:
            if "poster=" not in opening:
                return ""
            # its clean poster stays as a still
            block = block.replace(opening, re.sub(r"\s*\b(?:autoplay|loop|data-inview|tabindex=\"\d+\")", "", opening))
        return block

    html = re.sub(r"<video\b.*?</video>", video, html, flags=re.S)
    # whole blocks that still show one of them
    for tag in ("figure", "a"):
        html = re.sub(rf"<{tag}\b[^>]*>.*?</{tag}>", lambda m: "" if bad(m.group(0)) else m.group(0), html, flags=re.S)
    html = re.sub(r"<img\b[^>]*>", lambda m: "" if bad(m.group(0)) else m.group(0), html)
    html = re.sub(r"<meta\b[^>]*>", lambda m: "" if bad(m.group(0)) else m.group(0), html)
    return html


def takedown(site, flagged):
    site = site.resolve()
    gone = set()
    for f in sorted(site.rglob("*")):
        if f.suffix.lower() in MEDIA and f.is_file() and hashlib.sha256(f.read_bytes()).hexdigest() in flagged:
            gone.add(f.resolve())
            f.unlink()
            print(f"removed {f.relative_to(site)}")
    if not gone:
        return 0
    for page in sorted(site.rglob("*.html")):
        old = page.read_text(encoding="utf-8")
        new = strip(old, page.resolve(), site, gone)
        if new != old:
            page.write_text(new, encoding="utf-8")
            print(f"cleaned {page.relative_to(site)}")
    return len(gone)


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        site = Path(tmp)
        (site / "guide/images").mkdir(parents=True)
        (site / "media").mkdir()
        for name, data in (("guide/images/bad.jpg", b"bad"), ("guide/images/ok.jpg", b"ok"),
                           ("media/bad.mp4", b"badv"), ("media/ok.mp4", b"okv"), ("media/p.jpg", b"bad")):
            (site / name).write_bytes(data)
        (site / "guide/a.html").write_text(
            '<p>x</p><figure class="shot"><a href="images/bad.jpg"><img src="images/bad.jpg"></a></figure>'
            '<figure><img src="images/ok.jpg"></figure>')
        (site / "index.html").write_text(
            '<meta property="og:image" content="https://sculptforge.app/media/p.jpg?v=1">'
            '<video poster="media/p.jpg?v=2" muted><source src="media/bad.mp4?v=4"><source src="media/ok.mp4"></video>'
            '<video id="cam" autoplay></video><video src="media/bad.mp4"></video><img src="guide/images/bad.jpg" alt="">'
            '<figure><video id="h" poster="guide/images/ok.jpg" autoplay muted loop><source src="media/bad.mp4"></video></figure>'
            '<a href="media/bad.mp4">clip</a>, <a href="media/ok.mp4">ok</a>')
        flagged = {hashlib.sha256(d).hexdigest() for d in (b"bad", b"badv")}
        assert takedown(site, flagged) == 3
        assert (site / "guide/a.html").read_text() == '<p>x</p><figure><img src="images/ok.jpg"></figure>'
        assert (site / "index.html").read_text() == ('<video muted><source src="media/ok.mp4"></video><video id="cam" autoplay></video><figure><video id="h" poster="guide/images/ok.jpg" muted></video></figure>'
                                                   ', <a href="media/ok.mp4">ok</a>')
        assert (site / "guide/images/ok.jpg").exists() and not (site / "media/p.jpg").exists()
    print("media_takedown self-test ok")


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        self_test()
    else:
        print(f"media_takedown: {takedown(Path(sys.argv[1]), flagged_hashes())} file(s) taken down")
