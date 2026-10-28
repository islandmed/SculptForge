#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds the website's user guide (website/guide/*.html) from docs/guide/*.md.

    python3 tools/guide/build_site.py            # write website/guide
    python3 tools/guide/build_site.py --check    # also fail on broken links, images or characters
    python3 tools/guide/build_site.py --out DIR  # write somewhere else (tests)

Standard library only. It understands the same Markdown subset as the in-app reader (src/core/Guide.cpp):
one "# Title", "##"/"###" headings, paragraphs with **bold**, *italic*, `code`, <kbd>keys</kbd> and links,
"- "/"1. " lists with one nesting level, pipe tables, ``` code blocks, "> **Label:** ..." callouts, images on
their own line and "---" rules. The pages use website/style.css and website/motion.js.
"""
import argparse
import html
import json
import pathlib
import re
import shutil
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
SRC = ROOT / "docs" / "guide"
SITE = "https://sculptforge.app/"
CALLOUTS = {"tip": "Tip", "note": "Note", "zbrush": "ZBrush", "not-yet": "Not yet", "warning": "Warning",
            "important": "Important"}

# ---------------------------------------------------------------------------
# Markdown subset -> HTML
# ---------------------------------------------------------------------------


def slugify(text: str) -> str:
    """GitHub-style anchors, the same rule as guide::slugify."""
    out = []
    for ch in text:
        if ch.isascii() and ch.isalnum():
            out.append(ch.lower())
        elif ch == " ":
            out.append("-")
        elif ch in "-_" or ord(ch) >= 0x80:
            out.append(ch)
    return "".join(out)


def md_link(target: str) -> str:
    """docs/guide links (x.md#y) become site links (x.html#y)."""
    if re.match(r"^(https?:|mailto:)", target):
        return target
    path, _, anchor = target.partition("#")
    if path.endswith(".md"):
        path = path[:-3] + ".html"
    return path + ("#" + anchor if anchor else "")


ENTITIES = {"&amp;": "&", "&lt;": "<", "&gt;": ">", "&quot;": '"', "&nbsp;": " "}


def inline(text: str, links: list) -> str:
    """Inline formatting to HTML. Appends every link target to LINKS (for --check)."""
    out = []
    i = 0
    bold = italic = False
    n = len(text)
    while i < n:
        c = text[i]
        if c == "\\" and i + 1 < n and not text[i + 1].isalnum() and not text[i + 1].isspace():
            out.append(html.escape(text[i + 1]))
            i += 2
            continue
        if c == "`":
            close = text.find("`", i + 1)
            if close != -1:
                out.append("<code>" + html.escape(text[i + 1:close]) + "</code>")
                i = close + 1
                continue
        if text.startswith("<kbd>", i):
            close = text.find("</kbd>", i + 5)
            if close != -1:
                out.append("<kbd>" + html.escape(text[i + 5:close]) + "</kbd>")
                i = close + 6
                continue
        if c == "<" and (text.startswith("http://", i + 1) or text.startswith("https://", i + 1)):
            close = text.find(">", i + 1)
            if close != -1:
                url = text[i + 1:close]
                links.append(url)
                out.append(f'<a href="{html.escape(url)}">{html.escape(url)}</a>')
                i = close + 1
                continue
        if c == "&":
            for name, value in ENTITIES.items():
                if text.startswith(name, i):
                    out.append(html.escape(value))
                    i += len(name)
                    break
            else:
                out.append("&amp;")
                i += 1
            continue
        if text.startswith("**", i):
            out.append("</strong>" if bold else "<strong>")
            bold = not bold
            i += 2
            continue
        if c == "*":
            can_open = not italic and i + 1 < n and not text[i + 1].isspace() and text.find("*", i + 1) != -1
            can_close = italic and i > 0 and not text[i - 1].isspace()
            if can_open or can_close:
                out.append("</em>" if italic else "<em>")
                italic = not italic
                i += 1
                continue
        if c == "[":
            depth = 0
            close = -1
            k = i
            while k < n:
                if text[k] == "`":
                    e = text.find("`", k + 1)
                    if e == -1:
                        break
                    k = e + 1
                    continue
                if text[k] == "[":
                    depth += 1
                elif text[k] == "]":
                    depth -= 1
                    if depth == 0:
                        close = k
                        break
                k += 1
            if close != -1 and close + 1 < n and text[close + 1] == "(":
                end = text.find(")", close + 2)
                if end != -1:
                    target = text[close + 2:end].strip()
                    links.append(target)
                    label = inline(text[i + 1:close], [])
                    ext = ' rel="noopener"' if target.startswith("http") else ""
                    out.append(f'<a href="{html.escape(md_link(target))}"{ext}>{label}</a>')
                    i = end + 1
                    continue
        out.append(html.escape(c))
        i += 1
    if italic:
        out.append("</em>")
    if bold:
        out.append("</strong>")
    return "".join(out)


def table_cells(line: str) -> list:
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|") and not line.endswith("\\|"):
        line = line[:-1]
    cells, start, in_code, k = [], 0, False, 0
    while k <= len(line):
        if k < len(line) and not in_code and line.startswith("<kbd>", k):
            close = line.find("</kbd>", k)
            if close != -1:
                k = close + 6
                continue
        if k < len(line) and line[k] == "`":
            in_code = not in_code
        if k == len(line) or (line[k] == "|" and not in_code and (k == 0 or line[k - 1] != "\\")):
            cells.append(line[start:k].strip())
            start = k + 1
        k += 1
    return cells


def is_separator(line: str) -> bool:
    cells = table_cells(line)
    return bool(cells) and all(c and set(c) <= set("-: ") for c in cells)


class Page:
    def __init__(self, name: str, text: str):
        self.name = name  # "masking.md"
        self.title = ""
        self.headings = []  # (level, text, anchor)
        self.links = []
        self.images = []
        self.chars = text
        self.text_blocks = []  # (anchor, heading, plain text) for the search index
        self.body = self.render(text)

    def render(self, md: str) -> str:
        lines = md.split("\n")
        out = []
        used = set()
        para = []
        list_stack = []  # open list tags, by nesting level
        item_open = [False, False]
        section = ("", "")
        search_text = []

        def plain(s):
            return re.sub(r"<[^>]+>", "", html.unescape(s))

        def flush_para():
            if para:
                text = " ".join(para)
                rendered = inline(text, self.links)
                out.append(f"<p>{rendered}</p>")
                search_text.append(plain(rendered))
                para.clear()

        def close_lists(level=0):
            while len(list_stack) > level:
                lvl = len(list_stack) - 1
                if item_open[lvl]:
                    out.append("</li>")
                    item_open[lvl] = False
                out.append(f"</{list_stack.pop()}>")

        def end_section():
            if search_text:
                self.text_blocks.append((section[0], section[1], " ".join(search_text)))
                search_text.clear()

        i = 0
        pending_item = None  # [level, tag, text] being collected (wrapped list items)

        def flush_item():
            nonlocal pending_item
            if pending_item:
                lvl, tag, text = pending_item
                rendered = inline(text, self.links)
                out.append(rendered)
                search_text.append(plain(rendered))
                pending_item = None

        while i < len(lines):
            raw = lines[i]
            t = raw.strip()
            if not t:
                flush_para()
                flush_item()
                close_lists()
                i += 1
                continue
            if t.startswith("<!--"):
                flush_para(); flush_item(); close_lists()
                while i < len(lines) and "-->" not in lines[i]:
                    i += 1
                i += 1
                continue
            if t.startswith("```"):
                flush_para(); flush_item(); close_lists()
                lang = t[3:].strip()
                indent = len(raw) - len(raw.lstrip(" "))
                code = []
                i += 1
                while i < len(lines) and not lines[i].strip().startswith("```"):
                    line = lines[i]
                    drop = min(indent, len(line) - len(line.lstrip(" ")))
                    code.append(line[drop:])
                    i += 1
                i += 1
                cls = f' class="language-{html.escape(lang)}"' if lang else ""
                out.append(f'<div class="code-block"><pre class="code"><code{cls}>{html.escape(chr(10).join(code))}'
                           f'</code></pre><button class="copy" type="button" aria-label="Copy">Copy</button></div>')
                search_text.append(" ".join(code))
                continue
            m = re.match(r"^(#{1,6}) (.*)$", t)
            if m:
                flush_para(); flush_item(); close_lists()
                level = min(len(m.group(1)), 3)
                rendered = inline(m.group(2).strip(), self.links)
                text = plain(rendered)
                anchor = slugify(text)
                base, k = anchor, 1
                while anchor in used:
                    anchor = f"{base}-{k}"
                    k += 1
                used.add(anchor)
                if level == 1 and not self.title:
                    self.title = text
                    out.append(f'<h1 id="{anchor}">{rendered}</h1>')
                    section = ("", text)
                else:
                    end_section()
                    section = (anchor, text)
                    self.headings.append((level, text, anchor))
                    out.append(f'<h{level} id="{anchor}">{rendered}'
                               f'<a class="anchor" href="#{anchor}" aria-label="Link to this section">#</a></h{level}>')
                i += 1
                continue
            if t.startswith("|") and i + 1 < len(lines) and is_separator(lines[i + 1]):
                flush_para(); flush_item(); close_lists()
                rows = [table_cells(t)]
                i += 2
                while i < len(lines) and lines[i].strip().startswith("|"):
                    rows.append(table_cells(lines[i]))
                    i += 1
                cols = len(rows[0])
                head = "".join(f"<th>{inline(c, self.links)}</th>" for c in rows[0])
                body = "".join("<tr>" + "".join(f"<td>{inline(c, self.links)}</td>" for c in (r + [""] * cols)[:cols])
                               + "</tr>" for r in rows[1:])
                out.append(f'<div class="table-wrap"><table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table></div>')
                search_text.append(" ".join(plain(inline(c, [])) for r in rows for c in r))
                continue
            if t.startswith(">"):
                flush_para(); flush_item(); close_lists()
                parts = []
                while i < len(lines) and lines[i].strip().startswith(">"):
                    parts.append(lines[i].strip()[1:].strip())
                    i += 1
                text = " ".join(parts)
                kind = "note"
                m2 = re.match(r"^\*\*([^*]+?):?\*\*", text)
                if m2:
                    label = m2.group(1).rstrip(":").strip()
                    for key, name in CALLOUTS.items():
                        if label.lower() == name.lower():
                            kind = key
                rendered = inline(text, self.links)
                out.append(f'<aside class="callout callout-{kind}"><p>{rendered}</p></aside>')
                search_text.append(plain(rendered))
                continue
            m = re.match(r"^!\[(.*)\]\((.*)\)$", t)
            if m:
                flush_para(); flush_item(); close_lists()
                alt, src = m.group(1), m.group(2).strip()
                self.images.append(src)
                size = image_size(SRC / src)
                dims = f' width="{size[0]}" height="{size[1]}"' if size else ""
                out.append(f'<figure class="shot"><a href="{html.escape(src)}" class="zoom"><img src="{html.escape(src)}" '
                           f'alt="{html.escape(alt)}"{dims} loading="lazy" decoding="async"></a>'
                           f'<figcaption>{inline(alt, [])}</figcaption></figure>')
                search_text.append(alt)
                i += 1
                continue
            if re.match(r"^-{3,}$", t.replace(" ", "")) and not para:
                flush_item(); close_lists()
                out.append("<hr>")
                i += 1
                continue
            mb = re.match(r"^[-*+] (.*)$", t)
            mn = re.match(r"^(\d{1,4})[.)] (.*)$", t)
            if mb or mn:
                flush_para(); flush_item()
                indent = len(raw) - len(raw.lstrip(" "))
                level = 1 if indent >= 2 else 0
                tag = "ul" if mb else "ol"
                if len(list_stack) > level + 1 or (len(list_stack) == level + 1 and list_stack[level] != tag):
                    close_lists(level)
                if len(list_stack) == level + 1:
                    if item_open[level]:
                        out.append("</li>")
                elif len(list_stack) < level + 1:
                    while len(list_stack) < level + 1:
                        start = ""
                        if tag == "ol" and mn and mn.group(1) != "1":
                            start = f' start="{mn.group(1)}"'
                        out.append(f"<{tag}{start}>")
                        list_stack.append(tag)
                        item_open[len(list_stack) - 1] = False
                out.append("<li>")
                item_open[level] = True
                pending_item = [level, tag, (mb or mn).group(mb and 1 or 2)]
                i += 1
                continue
            if pending_item:
                pending_item[2] += " " + t
                i += 1
                continue
            para.append(t)
            i += 1
        flush_para()
        flush_item()
        close_lists()
        end_section()
        return "\n".join(out)


def image_size(path: pathlib.Path):
    """(width, height) of a JPEG or PNG, or None."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return struct.unpack(">II", data[16:24])
    if data[:2] == b"\xff\xd8":
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                i += 1
                continue
            marker = data[i + 1]
            if marker in (0xC0, 0xC1, 0xC2):
                h, w = struct.unpack(">HH", data[i + 5:i + 9])
                return w, h
            seg = struct.unpack(">H", data[i + 2:i + 4])[0]
            i += 2 + seg
    return None


# ---------------------------------------------------------------------------
# The book
# ---------------------------------------------------------------------------


def load_book():
    index = Page("index.md", (SRC / "index.md").read_text(encoding="utf-8"))
    chapters, group = [], ""
    seen = set()
    for line in (SRC / "index.md").read_text(encoding="utf-8").split("\n"):
        t = line.strip()
        if t.startswith("## "):
            group = t[3:].strip()
        m = re.match(r"^(?:\d+\.|[-*]) \[([^\]]+)\]\(([^)#]+\.md)\)", t)
        if m and m.group(2) not in seen:
            seen.add(m.group(2))
            path = SRC / m.group(2)
            if not path.exists():
                sys.exit(f"index.md lists {m.group(2)}, which doesn't exist")
            page = Page(m.group(2), path.read_text(encoding="utf-8"))
            chapters.append((group, page))
    return index, chapters


def page_html(page: Page, index: Page, chapters, position: int) -> str:
    current = page.name
    toc = ['<nav class="toc" aria-label="User guide">',
           f'<a class="toc-home{" current" if current == "index.md" else ""}" href="index.html">Contents</a>']
    group = None
    for g, ch in chapters:
        if g != group:
            if group is not None:
                toc.append("</ul>")
            toc.append(f'<p class="toc-group">{html.escape(g)}</p><ul>')
            group = g
        is_cur = ch.name == current
        href = ch.name[:-3] + ".html"
        current_attr = ' class="current" aria-current="page"' if is_cur else ""
        toc.append(f'<li><a href="{href}"{current_attr}>{html.escape(ch.title)}</a>')
        if is_cur:
            subs = [h for h in ch.headings if h[0] == 2]
            if subs:
                toc.append('<ul class="toc-sections">' + "".join(
                    f'<li><a href="#{a}">{html.escape(t)}</a></li>' for _, t, a in subs) + "</ul>")
        toc.append("</li>")
    toc.append("</ul></nav>")

    # Previous / next chapter.
    names = ["index.md"] + [c.name for _, c in chapters]
    titles = ["Contents"] + [c.title for _, c in chapters]
    k = names.index(current)
    pager = ['<nav class="pager" aria-label="Previous and next chapter">']
    if k > 0:
        pager.append(f'<a class="prev" rel="prev" href="{names[k - 1][:-3]}.html"><span>Previous</span>'
                     f'{html.escape(titles[k - 1])}</a>')
    if k + 1 < len(names):
        pager.append(f'<a class="next" rel="next" href="{names[k + 1][:-3]}.html"><span>Next</span>'
                     f'{html.escape(titles[k + 1])}</a>')
    pager.append("</nav>")

    on_page = [h for h in page.headings if h[0] == 2]
    aside = ""
    if len(on_page) >= 3:
        aside = ('<aside class="on-page" aria-label="On this page"><p>On this page</p><ul>' +
                 "".join(f'<li><a href="#{a}">{html.escape(t)}</a></li>' for _, t, a in on_page) + "</ul></aside>")

    title = "SculptForge User Guide" if current == "index.md" else f"{page.title} - SculptForge User Guide"
    first_p = re.search(r"<p>(.*?)</p>", page.body, re.S)
    desc = re.sub(r"<[^>]+>", "", first_p.group(1)) if first_p else "The SculptForge user guide."
    desc = html.escape(html.unescape(desc)[:300], quote=True)
    url = SITE + "guide/" + current[:-3] + ".html"
    edit = f"https://github.com/islandmed/SculptForge/edit/main/docs/guide/{current}"
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>{html.escape(title)}</title>
  <meta name="description" content="{desc}">
  <meta name="theme-color" content="#111114">
  <meta name="color-scheme" content="dark">
  <link rel="icon" type="image/svg+xml" href="../img/favicon.svg?v=2">
  <link rel="icon" type="image/png" href="../img/favicon-64.png?v=2">
  <link rel="apple-touch-icon" href="../img/icon-256.png?v=2">
  <link rel="canonical" href="{url}">
  <meta property="og:type" content="article">
  <meta property="og:title" content="{html.escape(title)}">
  <meta property="og:description" content="{desc}">
  <meta property="og:image" content="{SITE}img/social-preview.png?v=3">
  <meta property="og:url" content="{url}">
  <link rel="stylesheet" href="../style.css?v=17">
  <script src="../motion.js?v=10" defer></script>
  <script data-goatcounter="https://sculptforge.goatcounter.com/count" async src="https://gc.zgo.at/count.js"></script>
  <script src="search-index.js?v={len(chapters)}" defer></script>
</head>
<body class="guide-page" data-prev="{names[k - 1][:-3] + '.html' if k > 0 else ''}" data-next="{names[k + 1][:-3] + '.html' if k + 1 < len(names) else ''}">
  <div class="scroll-progress" aria-hidden="true"></div>
  <header class="nav">
    <div class="wrap nav-inner">
      <a class="brand" href="../index.html"><img src="../img/logo.svg?v=2" alt="" width="34" height="34"><span>SculptForge</span></a>
      <nav aria-label="Main">
        <a href="../index.html#features">Features</a>
        <a href="../index.html#demos">Demos</a>
        <a href="../brushes.html">Brushes</a>
        <a href="index.html" aria-current="page">Guide</a>
        <a class="nav-dl" href="../index.html#download">Download</a>
        <a class="gh" href="https://github.com/islandmed/SculptForge">GitHub</a>
      </nav>
    </div>
  </header>
  <div class="wrap guide-layout">
    <div class="guide-side">
      <button class="toc-toggle" type="button" aria-expanded="false" aria-controls="guide-toc">
        <span>Contents</span><span class="toc-current">{html.escape(page.title if current != "index.md" else "SculptForge User Guide")}</span></button>
      <div id="guide-toc" class="toc-panel">
        <div class="guide-search" role="search">
          <input type="search" id="guide-q" placeholder="Search the guide" aria-label="Search the guide" autocomplete="off">
          <div class="search-results" id="guide-results" hidden></div>
        </div>
        {"".join(toc)}
      </div>
    </div>
    <main class="guide-main" id="top">
      <article class="guide-article">
{page.body}
      </article>
      {"".join(pager)}
      <p class="guide-meta fine">Also in the app: <strong>Help &gt; User Guide</strong> (<kbd>Shift</kbd>+<kbd>F1</kbd>).
        Found a mistake? <a href="{edit}">Edit this page on GitHub</a>.</p>
    </main>
    {aside}
  </div>
  <footer class="footer">
    <div class="wrap footer-grid">
      <div>
        <a class="brand" href="../index.html"><img src="../img/logo.svg?v=2" alt="" width="28" height="28"><span>SculptForge</span></a>
        <p class="fine">Copyright © 2026 The SculptForge contributors.<br>The SculptForge User Guide is part of
          SculptForge, licensed under the <a href="https://github.com/islandmed/SculptForge/blob/main/LICENSE">GNU GPL v3.0 or later</a>.</p>
      </div>
      <div class="links">
        <a href="index.html">User guide</a>
        <a href="https://github.com/islandmed/SculptForge">Source code</a>
        <a href="https://github.com/islandmed/SculptForge/issues">Report a problem</a>
        <a href="https://github.com/islandmed/SculptForge/blob/main/docs/ai-services.md">AI services &amp; privacy</a>
      </div>
      <p class="fine trademarks">SculptForge is an independent project, not affiliated with or endorsed by Maxon or
        any other company named in this guide. ZBrush is a trademark of Maxon. All other trademarks belong to their
        owners.</p>
    </div>
  </footer>
</body>
</html>
"""


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------


def check(index: Page, chapters) -> list:
    problems = []
    pages = {"index.md": index}
    pages.update({c.name: c for _, c in chapters})
    anchors = {name: {a for _, _, a in p.headings} | ({slugify(p.title)} if p.title else set())
               for name, p in pages.items()}
    listed = set(pages)
    for f in sorted(SRC.glob("*.md")):
        if f.name not in listed:
            problems.append(f"{f.name}: not listed in index.md")
    for name, p in pages.items():
        if not p.title:
            problems.append(f"{name}: no '# Title' line")
        for lineno, line in enumerate(p.chars.split("\n"), 1):
            bad = [ch for ch in line if ord(ch) > 0xFF or (ord(ch) < 0x20 and ch != "\t")]
            if bad:
                problems.append(f"{name}:{lineno}: characters the app's font can't show: "
                                + " ".join(f"U+{ord(ch):04X}" for ch in sorted(set(bad))))
            if re.match(r"^#{4,} ", line):
                problems.append(f"{name}:{lineno}: only #, ## and ### headings are supported")
        for target in p.links:
            if re.match(r"^(https?:|mailto:)", target):
                continue
            path, _, anchor = target.partition("#")
            dest = path or name
            if dest not in pages:
                problems.append(f"{name}: link to {target}: no such chapter")
            elif anchor and anchor not in anchors[dest]:
                problems.append(f"{name}: link to {target}: no heading with that anchor in {dest}")
        for src in p.images:
            if not (SRC / src).is_file():
                problems.append(f"{name}: image {src} doesn't exist")
    used = {src for p in pages.values() for src in p.images}
    for img in sorted((SRC / "images").glob("*")):
        if f"images/{img.name}" not in used:
            problems.append(f"images/{img.name}: not used by any page")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "website" / "guide")
    ap.add_argument("--check", action="store_true", help="fail on broken links, missing images or bad characters")
    args = ap.parse_args()
    index, chapters = load_book()
    if args.check:
        problems = check(index, chapters)
        for p in problems:
            print(p, file=sys.stderr)
        if problems:
            return 1
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("*.html"):
        old.unlink()
    pages = [index] + [c for _, c in chapters]
    for k, page in enumerate(pages):
        (out / (page.name[:-3] + ".html")).write_text(page_html(page, index, chapters, k), encoding="utf-8")
    images = out / "images"
    if images.exists():
        shutil.rmtree(images)
    shutil.copytree(SRC / "images", images)
    # Search: one entry per section.
    entries = []
    for page in pages:
        for anchor, heading, text in page.text_blocks:
            entries.append({"p": page.name[:-3] + ".html", "c": page.title, "a": anchor, "h": heading,
                            "t": re.sub(r"\s+", " ", text)[:4000]})
    (out / "search-index.js").write_text("window.GUIDE_INDEX=" + json.dumps(entries, ensure_ascii=False,
                                                                             separators=(",", ":")) + ";\n",
                                         encoding="utf-8")
    # The site's sitemap (website/sitemap.xml, named in website/robots.txt): the homepage, the brushes page and
    # every guide page. Only for the real site, not --out folders.
    if out.resolve() == (ROOT / "website" / "guide").resolve():
        urls = [SITE, SITE + "brushes.html", SITE + "mudbox-alternative.html",
                SITE + "zbrush-alternative.html", SITE + "zbrush-linux.html"] + [f"{SITE}guide/{p.name[:-3]}.html" for p in pages]
        (ROOT / "website" / "sitemap.xml").write_text(
            '<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
            + "".join(f"  <url><loc>{html.escape(u)}</loc></url>\n" for u in urls) + "</urlset>\n", encoding="utf-8")
    print(f"wrote {len(pages)} pages and {len(list(images.glob('*')))} images to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
