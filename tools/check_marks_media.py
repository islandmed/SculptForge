#!/usr/bin/env python3
"""Fails when a third-party sculpting-app trademark or old interface label is readable in a tracked image or video.

check_marks.py reads text; this reads pixels. Every git-tracked image is OCR'd (tesseract), every video and GIF is
sampled at one frame per second and each frame OCR'd. Each line of text is matched against check_marks' patterns
(docs/legal/trademarks.md, the one file that spells them), as read and with dashes, underscores and runs of spaces
taken out. The hit count per frame is cached by file hash in tools/marks_media_cache.json (committed; counts only,
never the words; tools/marks_media_flagged.txt, what the deploy takes down, is written from it), so CI only reads new or changed files; a new file that can't be read here (no tesseract) fails
until the cache has it. Name files on the command line to OCR them afresh and see the words.
Files listed in tools/marks_media_pending.txt (shots waiting to be re-recorded) only warn; the deploy takes them down
(media_takedown.py). A listed file that reads clean fails until it's taken off the list, so the list only shrinks.

  check_marks_media.py               check every tracked image and video (ctest: marks_media)
  check_marks_media.py FILE...       check these files (any path), print every hit with its frame time
  check_marks_media.py --jobs N      parallel OCR jobs (default 3: the laptop's heavy-job budget)
  check_marks_media.py --self-test
"""
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_marks  # noqa: E402

ROOT = check_marks.ROOT
CACHE = ROOT / "tools" / "marks_media_cache.json"
PENDING = ROOT / "tools" / "marks_media_pending.txt"
FLAGGED = ROOT / "tools" / "marks_media_flagged.txt"  # what the deploy takes down (media_takedown.py), from the cache
CACHE_VERSION = 2  # bump when the OCR or the patterns change in a way that should re-read every file
IMAGES = (".png", ".jpg", ".jpeg", ".webp", ".avif", ".bmp", ".tif", ".tiff")
VIDEOS = (".mp4", ".webm", ".mov", ".mkv", ".gif", ".m4v")
SKIP_PREFIXES = check_marks.SKIP_PREFIXES


def media_files():
    files = check_marks.tracked_files() or []
    return [p for p in files if p.lower().endswith(IMAGES + VIDEOS) and not p.startswith(SKIP_PREFIXES)]


def hits_in(text, mark_re):
    """Sorted distinct words that look like a mark in OCR text."""
    found = set()
    for line in text.splitlines():
        for variant in (line, re.sub(r"[-_]", "", line), re.sub(r"\s+", " ", line), re.sub(r"[-_\s]", "", line)):
            found.update(m.group(0).strip() for m in mark_re.finditer(variant))
    return sorted({re.sub(r"\W", "", w.lower()): w for w in sorted(found)}.values(), key=str.lower)


def ocr(png):
    """Text of one image, read twice (sparse UI text, and blocks) on a grey, dark-on-light, at-least-2000-px copy."""
    from PIL import Image, ImageOps, ImageStat
    with tempfile.TemporaryDirectory() as tmp:
        img = Image.open(png)
        img.seek(0)
        img = img.convert("L")
        if ImageStat.Stat(img).mean[0] < 110:
            img = ImageOps.invert(img)  # the app is dark: tesseract reads dark text on light best
        scale = min(3.0, 2000 / max(img.width, 1))
        if scale > 1.05:
            img = img.resize((round(img.width * scale), round(img.height * scale)), Image.LANCZOS)
        prep = Path(tmp) / "p.png"
        img.save(prep)
        out = []
        for psm in ("11", "3"):
            r = subprocess.run(["tesseract", str(prep), "-", "--psm", psm], capture_output=True, text=True)
            out.append(r.stdout)
        return "\n".join(out)


def scan_file(path, mark_re):
    """[(time or None, [words])] for the frames of one file with a hit."""
    path = Path(path)
    if path.suffix.lower() in IMAGES:
        words = hits_in(ocr(path), mark_re)
        return [[None, words]] if words else []
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-vf", "fps=1", f"{tmp}/f%05d.png"], check=True)
        hits = []
        for frame in sorted(Path(tmp).glob("f*.png")):
            words = hits_in(ocr(frame), mark_re)
            if words:
                hits.append([int(frame.stem[1:]) - 1, words])  # frame n of fps=1 is second n-1
        return hits


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_cache():
    try:
        data = json.loads(CACHE.read_text())
        return data["files"] if data.get("version") == CACHE_VERSION else {}
    except (OSError, ValueError, KeyError):
        return {}


def save_cache(files):
    CACHE.write_text(json.dumps({"version": CACHE_VERSION, "files": dict(sorted(files.items()))}, indent=0) + "\n")
    lines = FLAGGED.read_text().splitlines() if FLAGGED.exists() else []
    header = [l for l in lines if l.startswith("#")]
    hashes = {l.strip() for l in lines if l.strip() and not l.startswith("#")} | {h for h, hits in files.items() if hits}
    FLAGGED.write_text("\n".join(header + sorted(hashes)) + "\n")


def counts(hits):
    """What the cache keeps of a file's hits: the number of words per frame, never the words."""
    return [[t, len(words)] for t, words in hits]


def describe(path, hits):
    lines = []
    for t, found in hits:
        where = f" @ {t // 60}:{t % 60:02d}" if t is not None else ""
        what = ", ".join(found) if isinstance(found, list) else f"{found} mark(s); check_marks_media.py {path} lists them"
        lines.append(f"{path}{where}: {what}")
    return lines


def main(argv):
    if "--self-test" in argv:
        # the words come from docs/legal/trademarks.md: this file spells none of them
        mark_re = check_marks.load_marks()
        for old, today in check_marks.old_words():
            assert len(hits_in(f"Brush  {old.upper()}  25\nx", mark_re)) == 1, old
            assert len(hits_in(f"{old[:2]}-{old[2:]} list", mark_re)) == 1, old  # OCR splits words
            assert not hits_in(f"Brush  {today}  25", mark_re), today
        assert not hits_in("Forge Spheres  Lit Sphere  Part  Face group  Library  Vertex Paint  Strength", mark_re)
        assert counts([[None, ["a", "b"]], [3, ["c"]]]) == [[None, 2], [3, 1]]
        print("check_marks_media self-test ok")
        return 0
    jobs = int(argv[argv.index("--jobs") + 1]) if "--jobs" in argv else 3
    explicit = [a for a in argv if not a.startswith("--") and not a.isdigit()]
    mark_re = check_marks.load_marks()
    have_ocr = bool(shutil.which("tesseract") and shutil.which("ffmpeg"))
    cache = {} if explicit else load_cache()  # named files are read afresh, so their words can be shown
    if not explicit and check_marks.tracked_files() is None:
        print("check_marks_media: skipped, git can't list the tracked files here")
        return 0
    paths = explicit or media_files()
    keys = {p: sha(ROOT / p if not explicit else p) for p in paths}
    todo = [p for p in paths if keys[p] not in cache]
    if todo and not have_ocr:
        for p in todo:
            print(f"{p}: not in tools/marks_media_cache.json and tesseract/ffmpeg aren't here; "
                  "run tools/check_marks_media.py where they are and commit the cache")
        return 1

    def work(p):
        return p, scan_file(ROOT / p if not explicit else p, mark_re)

    with ThreadPoolExecutor(max(1, jobs)) as pool:
        for n, (p, hits) in enumerate(pool.map(work, todo), 1):
            cache[keys[p]] = hits if explicit else counts(hits)
            print(f"[{n}/{len(todo)}] {p}: {'HIT' if hits else 'clean'}", file=sys.stderr)
    if not explicit:
        live = set(keys.values())
        # a flagged file stays flagged after it's replaced: a release tag's guide still has it (media_takedown.py)
        save_cache({k: v for k, v in cache.items() if k in live or v})
    pending = set() if explicit else {l.strip() for l in PENDING.read_text().splitlines() if l.strip() and not l.startswith("#")}
    errors, waiting = [], 0
    for p in paths:
        if p in pending:
            waiting += bool(cache[keys[p]])
            if not cache[keys[p]]:
                errors.append(f"{p}: clean now; take it off tools/marks_media_pending.txt")
        else:
            errors += describe(p, cache[keys[p]])
    if waiting:
        print(f"check_marks_media: {waiting} file(s) still waiting to be re-recorded (tools/marks_media_pending.txt; "
              "the site deploy leaves them out)")
    for e in errors:
        print(e)
    if errors:
        print(f"\n{len(errors)} image/frame(s) show a mark or an old label (docs/legal/trademarks.md). Swap in a "
              "clean capture from current main.", file=sys.stderr)
        return 1
    print(f"check_marks_media: {len(paths)} images/videos, no marks on screen")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
