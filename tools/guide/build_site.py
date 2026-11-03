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
# Chapters that were renamed: the old page stays as a small redirect so links from elsewhere keep working.
REDIRECTS = {"subtools.html": "parts.html", "zspheres.html": "forge-spheres.html", "polypaint.html": "vertex-paint.html",
             "polygroups.html": "face-groups.html"}
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
    def __init__(self, name: str, text: str, used=None, shift: int = 0):
        self.name = name  # "masking.md"
        self.used = set() if used is None else used  # heading anchors taken (shared by What's New's parts)
        self.shift = shift  # added to heading levels (What's New's folded versions: ### becomes h4)
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
        used = self.used
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
                if level > 1:
                    level = min(level + self.shift, 4)
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
# What's New (whats-new.md): the data in each version's ```features block, the newest versions in full and the older
# ones folded into groups. Mirrors src/core/WhatsNew.cpp, which the app uses for the same chapter and its pop-up.
# ---------------------------------------------------------------------------

WHATS_NEW = "whats-new.md"
WN_DETAILED, WN_SERIES_GROUPS, WN_MAX_HIGHLIGHTS = 3, 2, 6
WN_KEYS = {"date", "tagline", "highlight", "title", "line", "image", "action", "link"}


def version_parts(v: str) -> list:
    return [int(x) if x.isdigit() else 0 for x in v.split(".")]


def compare_versions(a: str, b: str) -> int:
    x, y = version_parts(a), version_parts(b)
    n = max(len(x), len(y))
    x, y = x + [0] * (n - len(x)), y + [0] * (n - len(y))
    return (x > y) - (x < y)


def series(v: str) -> str:
    p = version_parts(v) + [0, 0]
    return f"{p[0]}.{p[1]}"


def project_version() -> str:
    m = re.search(r"project\(SculptForge VERSION (\S+)", (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    return m.group(1) if m else ""


def parse_features(text: str) -> dict:
    """The features block: date, tagline, highlights, then cards (each starts with title:). Raises ValueError."""
    v = {"date": "", "tagline": "", "highlights": [], "cards": []}
    for k, raw in enumerate(text.split("\n"), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, value = line.partition(":")
        key, value = key.strip(), value.strip()
        if not sep or key not in WN_KEYS:
            raise ValueError(f"line {k}: " + (f"unknown key '{key}'" if sep else "expected 'key: value'"))
        if key == "title":
            v["cards"].append({"title": value, "line": "", "image": "", "action": "", "link": ""})
        elif key in ("line", "image", "action", "link"):
            if not v["cards"]:
                raise ValueError(f"line {k}: '{key}' before the card's 'title'")
            v["cards"][-1][key] = value
        elif key == "highlight":
            v["highlights"].append(value)
        else:
            v[key] = value
    for c in v["cards"]:
        if not (c["title"] and c["line"] and c["action"]):
            raise ValueError(f"card '{c['title']}' needs a title, a line and an action")
    return v


def split_highlight(h: str):
    title, sep, rest = h.partition(": ")
    return (title.strip(), rest.strip()) if sep else ("", h)


def build_whats_new(md: str, current: str):
    """(intro markdown, versions, groups, problems). Versions newer than CURRENT are left out; each version has
    version, anchor, date, tagline, highlights, cards and body (its markdown without the heading and the block)."""
    lines = md.split("\n")
    starts = [i for i, l in enumerate(lines) if re.match(r"^## Version \S+$", l.strip())]
    intro = "\n".join(lines[:starts[0]] if starts else lines)
    versions, problems = [], []
    for n, s in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else len(lines)
        name = lines[s].strip()[len("## Version "):]
        body = lines[s + 1:end]
        data = {"date": "", "tagline": "", "highlights": [], "cards": []}
        k = 0
        while k < len(body) and not body[k].strip():
            k += 1
        if k < len(body) and body[k].strip() == "```features":
            e = k + 1
            while e < len(body) and body[e].strip() != "```":
                e += 1
            try:
                data = parse_features("\n".join(body[k + 1:e]))
            except ValueError as err:
                problems.append(f"{WHATS_NEW}: Version {name}: {err}")
            body = body[:k] + body[e + 1:]
        if current and compare_versions(name, current) > 0:
            continue  # not released yet
        versions.append(dict(data, version=name, anchor=slugify("Version " + name), body="\n".join(body)))
    detailed = min(WN_DETAILED, len(versions))
    groups, seen = [], []
    for i in range(detailed, len(versions)):
        s = series(versions[i]["version"])
        if s not in seen:
            seen.append(s)
        slot = min(seen.index(s), WN_SERIES_GROUPS)
        while len(groups) <= slot:
            groups.append({"versions": []})
        groups[slot]["versions"].append(i)
    for g, group in enumerate(groups):
        newest, oldest = versions[group["versions"][0]], versions[group["versions"][-1]]
        if len(group["versions"]) == 1:
            label = newest["version"]
        elif g == WN_SERIES_GROUPS:
            label = series(newest["version"]) + " and earlier"
        elif any(series(versions[i]["version"]) == series(newest["version"]) for i in range(detailed)):
            label = f"{oldest['version']} to {newest['version']}"
        else:
            label = series(newest["version"]) + ".x"
        order = sorted(group["versions"], key=lambda i: -len(versions[i]["highlights"]))  # stable: newest first on ties
        picked, rnd = [], 0
        while len(picked) < WN_MAX_HIGHLIGHTS:
            any_left = False
            for i in order:
                hs = versions[i]["highlights"]
                if rnd < len(hs):
                    any_left = True
                    if len(picked) < WN_MAX_HIGHLIGHTS:
                        picked.append((i, hs[rnd]))
            if not any_left:
                break
            rnd += 1
        picked.sort(key=lambda p: p[0])
        group.update(label=label, anchor="versions-" + slugify(label), highlights=[h for _, h in picked])
        group["summary"] = ", ".join(t for t, _ in map(split_highlight, group["highlights"]) if t)
    return intro, versions, groups, problems


def card_link(card: dict) -> str:
    """Where a card's button goes on the website: its link, else its guide: action; "" for app-only actions."""
    target = card.get("link") or (card["action"][len("guide:"):] if card["action"].startswith("guide:") else "")
    return md_link(target) if target else ""


class WhatsNewPage(Page):
    """whats-new.md: version bands with their feature cards and "All changes", then the folded groups."""

    def __init__(self, name: str, text: str):
        intro, self.versions, self.groups, self.problems = build_whats_new(text, project_version())
        self.detailed = min(WN_DETAILED, len(self.versions))
        # The plain chapter (released versions, no data blocks): headings, links, search and the checks.
        plain = intro + "\n" + "\n".join(f"## Version {v['version']}\n{v['body']}" for v in self.versions)
        super().__init__(name, plain)
        self.chars = text
        # Every card picture, also those of a version not released yet (it ships with the next one).
        self.images += [src for src in re.findall(r"^image:\s*(\S+)\s*$", text, re.M) if src not in self.images]
        for v in self.versions:
            for c in v["cards"]:
                if c.get("link"):
                    self.links.append(c["link"])
                elif c["action"].startswith("guide:"):
                    self.links.append(c["action"][len("guide:"):])
        self.toc_sections = [(f"Version {v['version']}", v["anchor"]) for v in self.versions[:self.detailed]] + \
                            [(g["label"], g["anchor"]) for g in self.groups]
        self.extra_anchors = {g["anchor"] for g in self.groups}
        self.body = self.render_whats_new(intro)

    def render_whats_new(self, intro: str) -> str:
        used = set()
        out = [Page(self.name, intro, used).body]

        def cards(v):
            items = []
            for c in v["cards"]:
                img = ""
                if c["image"]:
                    size = image_size(SRC / c["image"])
                    dims = f' width="{size[0]}" height="{size[1]}"' if size else ""
                    img = (f'<img src="{html.escape(c["image"])}" alt=""{dims} loading="lazy" decoding="async">')
                href = card_link(c)
                more = (f'<a class="wn-more" href="{html.escape(href)}">'
                        f'{"Read more" if c["action"].startswith("guide:") else "How it works"}'
                        f'<span aria-hidden="true"> &rarr;</span></a>') if href else ""
                items.append(f'<article class="wn-card"><div class="wn-pic">{img}</div><div class="wn-card-body">'
                             f'<h3>{inline(c["title"], [])}</h3><p>{inline(c["line"], [])}</p>{more}</div></article>')
            return f'<div class="wn-cards wn-n{len(items)}">' + "".join(items) + "</div>" if items else ""

        def date_html(v):
            return f'<time datetime="{html.escape(v["date"])}">{html.escape(v["date"])}</time>' if v["date"] else ""

        for n, v in enumerate(self.versions[:self.detailed]):
            used.add(v["anchor"])
            body = Page(self.name, v["body"], used).body
            out.append(
                f'<section class="wn-version" id="{v["anchor"]}"><header class="wn-band">'
                f'<h2>Version {html.escape(v["version"])}<a class="anchor" href="#{v["anchor"]}" '
                f'aria-label="Link to this version">#</a></h2>{date_html(v)}'
                + (f'<p class="wn-tagline">{inline(v["tagline"], [])}</p>' if v["tagline"] else "") + "</header>"
                + cards(v)
                + f'<details class="wn-all"{" open" if n == 0 else ""}><summary>All changes</summary>'
                  f'<div class="wn-body">{body}</div></details></section>')
        if self.groups:
            out.append('<p class="wn-earlier">Earlier versions</p>')
        for g in self.groups:
            used.add(g["anchor"])
            items = "".join(f"<li><strong>{inline(t, [])}</strong>: {inline(r, [])}</li>" if t else f"<li>{inline(r, [])}</li>"
                            for t, r in map(split_highlight, g["highlights"]))
            inner = []
            for i in g["versions"]:
                v = self.versions[i]
                used.add(v["anchor"])
                inner.append(f'<h3 id="{v["anchor"]}" class="wn-ver">Version {html.escape(v["version"])}'
                             f'{date_html(v)}</h3>' + Page(self.name, v["body"], used, shift=1).body)
            out.append(
                f'<section class="wn-group" id="{g["anchor"]}"><header class="wn-band wn-band-small">'
                f'<h2>{html.escape(g["label"])}<a class="anchor" href="#{g["anchor"]}" aria-label="Link to these versions">#</a>'
                f'</h2></header><ul class="wn-highlights">{items}</ul>'
                f'<details class="wn-all"><summary>All changes</summary><div class="wn-body">{"".join(inner)}</div>'
                f'</details></section>')
        # A link into a folded section opens it.
        out.append("<script>(function(){function o(){var h=location.hash.slice(1);if(!h)return;var e=document.getElementById(h);"
                   "for(var p=e;p;p=p.parentElement)if(p.tagName==='DETAILS')p.open=true;if(e)e.scrollIntoView();}"
                   "addEventListener('hashchange',o);o();})();</script>")
        return "\n".join(out)



# The home page's release area (website/index.html #whats-new, between the whats-new markers): the newest versions
# as cards with their first picture, then the older ones as groups of highlights, all linking into the guide chapter.
HOME = ROOT / "website" / "index.html"
HOME_START, HOME_END = "<!-- whats-new:start", "<!-- whats-new:end -->"
CHANGELOG_URL = "https://github.com/islandmed/SculptForge/blob/main/CHANGELOG.md"


def home_whats_new(page: "WhatsNewPage") -> str:
    vs = page.versions[:page.detailed]
    out = []
    if vs:
        lede = vs[0]["tagline"] or vs[0]["version"]
        out.append(f'        <p class="section-lede">New in {html.escape(vs[0]["version"])}: {inline(lede, [])} '
                   f'<a href="guide/whats-new.html">Everything that changed</a> &middot; <a href="{CHANGELOG_URL}">changelog</a></p>')
    out.append('        <div class="wn-home">')
    for v in vs:
        titles = [split_highlight(h)[0] for h in v["highlights"] if split_highlight(h)[0]] or [c["title"] for c in v["cards"]]
        pic = next((c["image"] for c in v["cards"] if c["image"]), "")
        img = (f'<div class="wn-pic"><img src="guide/{html.escape(pic)}" alt="" width="600" height="340" loading="lazy" '
               f'decoding="async"></div>') if pic else \
            '<div class="wn-pic wn-pic-mark"><img src="img/logo.svg?v=2" alt="" width="64" height="64"></div>'
        items = "".join(f"<li>{inline(c['title'], [])}</li>" for c in v["cards"]) or \
            "".join(f"<li>{inline(r[:1].upper() + r[1:], [])}</li>" for _, r in map(split_highlight, v["highlights"]))
        out.append(f'          <article class="wn-home-card"><a href="guide/whats-new.html#{v["anchor"]}">{img}'
                   f'<div class="wn-card-body"><h3>{html.escape(", ".join(titles[:3]) or "Version " + v["version"])} '
                   f'<small>new in {html.escape(v["version"])}</small></h3>'
                   + (f'<p>{inline(v["tagline"], [])}</p>' if v["tagline"] else "")
                   + f'<ul>{items}</ul></div></a></article>')
    out.append("        </div>")
    if page.groups:
        out.append('        <div class="wn-home-groups">')
        for g in page.groups:
            items = "".join(f"<li>{inline(tt, [])}</li>" for tt, _ in map(split_highlight, g["highlights"]) if tt)
            out.append(f'          <a class="wn-home-group" href="guide/whats-new.html#{g["anchor"]}"><h3>{html.escape(g["label"])}</h3>'
                       f'<ul>{items}</ul><span>All changes &rarr;</span></a>')
        out.append("        </div>")
    return "\n".join(out)


def write_home(page: "WhatsNewPage", check_only: bool) -> bool:
    """Writes the release area into website/index.html; with CHECK_ONLY, True when it is already up to date."""
    t = HOME.read_text(encoding="utf-8")
    a = t.find(HOME_START)
    b = t.find(HOME_END, a)
    if a < 0 or b < 0:
        sys.exit(f"{HOME}: no {HOME_START} ... {HOME_END} markers in #whats-new")
    a = t.index("\n", a) + 1
    new = t[:a] + home_whats_new(page) + "\n        " + t[b:]
    if check_only:
        return new == t
    HOME.write_text(new, encoding="utf-8")
    return True

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
            cls = WhatsNewPage if m.group(2) == WHATS_NEW else Page
            page = cls(m.group(2), path.read_text(encoding="utf-8"))
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
            subs = getattr(ch, "toc_sections", None) or [(t, a) for lvl, t, a in ch.headings if lvl == 2]
            if subs:
                toc.append('<ul class="toc-sections">' + "".join(
                    f'<li><a href="#{a}">{html.escape(t)}</a></li>' for t, a in subs) + "</ul>")
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

    on_page = getattr(page, "toc_sections", None) or [(t, a) for lvl, t, a in page.headings if lvl == 2]
    aside = ""
    if len(on_page) >= 3:
        aside = ('<aside class="on-page" aria-label="On this page"><p>On this page</p><ul>' +
                 "".join(f'<li><a href="#{a}">{html.escape(t)}</a></li>' for t, a in on_page) + "</ul></aside>")

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
  <link rel="stylesheet" href="../style.css?v=19">
  <script src="../motion.js?v=11" defer></script>
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
        <a class="gh" href="https://discord.gg/gWrYkEmQhH">Discord</a>
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
        <a href="../supporters.html">Supporters</a>
        <a href="https://github.com/islandmed/SculptForge">Source code</a>
        <a href="https://discord.gg/gWrYkEmQhH">Community Discord</a>
        <a href="https://github.com/islandmed/SculptForge/issues">Report a problem</a>
        <a href="https://github.com/islandmed/SculptForge/blob/main/docs/ai-services.md">AI services &amp; privacy</a>
      </div>
      <p class="fine trademarks">ZBrush, GoZ, MatCap and other ZBrush feature names are trademarks of Maxon Computer
        GmbH. SculptForge is not affiliated with or endorsed by Maxon. All other trademarks belong to their owners.</p>
    </div>
  </footer>
</body>
</html>
"""


def redirect_html(target: str) -> str:
    """A page that sends visitors (and search engines) on to a renamed chapter."""
    url = SITE + "guide/" + target
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Moved - SculptForge User Guide</title>
  <meta name="robots" content="noindex">
  <link rel="canonical" href="{url}">
  <meta http-equiv="refresh" content="0; url={target}">
</head>
<body>
  <p>This chapter has moved to <a href="{target}">{url}</a>.</p>
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
               | getattr(p, "extra_anchors", set()) for name, p in pages.items()}
    wn = pages.get(WHATS_NEW)
    if isinstance(wn, WhatsNewPage):
        problems += wn.problems
        for v in wn.versions:
            if not v["date"]:
                problems.append(f"{WHATS_NEW}: Version {v['version']} has no ```features block with its date")
            for c in v["cards"]:
                if not 1 <= len(c["title"].split()) <= 6:
                    problems.append(f"{WHATS_NEW}: Version {v['version']}: card title '{c['title']}' should be 3-5 words")
                if c["image"] and c["image"].startswith("images/wn-") and image_size(SRC / c["image"]) not in (None, (600, 340)):
                    problems.append(f"{WHATS_NEW}: {c['image']} should be 600x340 (tools/guide/make_images.py --whats-new)")
            if v["cards"] and not 1 <= len(v["cards"]) <= 5:
                problems.append(f"{WHATS_NEW}: Version {v['version']} has {len(v['cards'])} cards; the window shows 1 to 5")
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
    for old_name, new_name in REDIRECTS.items():
        if new_name[:-5] + ".md" not in pages:
            problems.append(f"REDIRECTS: {old_name} points to {new_name}, which isn't a chapter")
        if old_name[:-5] + ".md" in pages:
            problems.append(f"REDIRECTS: {old_name} is a chapter again; remove the redirect")
    used = {src for p in pages.values() for src in p.images}
    for img in sorted(f for f in (SRC / "images").rglob("*") if f.is_file()):  # also images/<chapter>/*
        rel = img.relative_to(SRC).as_posix()
        if rel not in used:
            problems.append(f"{rel}: not used by any page")
    return problems


# ---------------------------------------------------------------------------
# website/supporters.html: the credits on the website, from docs/supporters.txt (the list Help > About shows)
# ---------------------------------------------------------------------------

SUPPORTERS = ROOT / "docs" / "supporters.txt"
SUPPORTERS_PAGE = ROOT / "website" / "supporters.html"


def supporter_names(text: str) -> list:
    """The same rules as parseSupporters in src/app/Support.h: one name per line, '#' lines and blank lines skipped,
    invalid UTF-8, control and invisible formatting characters dropped, spaces trimmed, 40 characters, 2000 names."""
    def hidden(c: str) -> bool:
        cp = ord(c)
        return (cp < 0x20 or 0x7F <= cp <= 0x9F or 0x200B <= cp <= 0x200F or 0x2028 <= cp <= 0x202E
                or 0x2060 <= cp <= 0x2069 or cp == 0xFEFF)
    names = []
    for line in text.split("\n"):
        if len(names) >= 2000:
            break
        if not line.strip(" \t\r") or line.lstrip(" \t\r").startswith("#"):
            continue
        kept = "".join(c for c in line if not hidden(c) and not 0xD800 <= ord(c) <= 0xDFFF)[:40].strip(" ")
        if kept:
            names.append(kept)
    return names


def supporters_html() -> str:
    raw = SUPPORTERS.read_bytes().decode("utf-8", errors="ignore") if SUPPORTERS.exists() else ""
    names = supporter_names(raw)
    if names:
        body = ('<ul class="supporters">\n' + "".join(f"          <li>{html.escape(n)}</li>\n" for n in names)
                + "        </ul>")
    else:
        body = '<p class="lede">Be the first: <a href="support.html">become a member</a>.</p>'
    return f"""<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Supporters</title>
  <meta name="description" content="The members who keep SculptForge free and open source for everyone.">
  <meta name="color-scheme" content="dark light">
  <link rel="icon" type="image/svg+xml" href="img/favicon.svg?v=2">
  <link rel="canonical" href="{SITE}supporters.html">
  <link rel="stylesheet" href="style.css?v=18">
  <style>
    .supporters {{ columns: 3 14em; column-gap: 32px; padding: 0; list-style: none; }}
    .supporters li {{ break-inside: avoid; padding: 4px 0; }}
  </style>
  <!-- Generated by tools/guide/build_site.py from docs/supporters.txt; edit that file, not this one. -->
</head>
<body>
  <header class="nav">
    <div class="wrap nav-inner">
      <a class="brand" href="index.html"><img src="img/logo.svg?v=2" alt="" width="34" height="34"><span>SculptForge</span></a>
      <nav aria-label="Main">
        <a href="guide/index.html">Guide</a>
        <a href="studios.html">Studios</a>
        <a class="nav-support" href="support.html">Support</a>
        <a class="nav-dl" href="index.html#download">Download</a>
      </nav>
    </div>
  </header>
  <main id="top">
    <section class="section">
      <div class="wrap">
        <p class="eyebrow">Credits</p>
        <h1>Supporters</h1>
        <p>Thank you to everyone who supports SculptForge. The same names are in the app, under Help &gt; About.</p>
        {body}
      </div>
    </section>
  </main>
  <footer class="footer">
    <div class="wrap footer-grid">
      <div class="links">
        <a href="index.html">Home</a>
        <a href="support.html">Membership</a>
        <a href="https://github.com/islandmed/SculptForge">Source code</a>
      </div>
      <p class="fine">Copyright © 2026 The SculptForge contributors.</p>
    </div>
  </footer>
</body>
</html>
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "website" / "guide")
    ap.add_argument("--check", action="store_true", help="fail on broken links, missing images or bad characters")
    ap.add_argument("--supporters", action="store_true", help="only write website/supporters.html (the site deploy)")
    args = ap.parse_args()
    if args.supporters:
        SUPPORTERS_PAGE.write_text(supporters_html(), encoding="utf-8")
        return 0
    if args.check and SUPPORTERS_PAGE.exists() and SUPPORTERS_PAGE.read_text(encoding="utf-8") != supporters_html():
        print("website/supporters.html is out of date with docs/supporters.txt: run tools/guide/build_site.py",
              file=sys.stderr)
        return 1
    index, chapters = load_book()
    wn = next((c for _, c in chapters if isinstance(c, WhatsNewPage)), None)
    if args.check:
        problems = check(index, chapters)
        if wn and args.out.resolve() == (ROOT / "website" / "guide").resolve() and not write_home(wn, True):
            problems.append("website/index.html: the What's New area is out of date with whats-new.md: run tools/guide/build_site.py")
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
    for old_name, new_name in REDIRECTS.items():
        (out / old_name).write_text(redirect_html(new_name), encoding="utf-8")
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
        SUPPORTERS_PAGE.write_text(supporters_html(), encoding="utf-8")
        if wn:
            write_home(wn, False)
        urls = [SITE, SITE + "brushes.html", SITE + "mudbox-alternative.html",
                SITE + "zbrush-alternative.html", SITE + "zbrush-linux.html", SITE + "press.html",
                SITE + "studios.html", SITE + "support.html", SITE + "supporters.html"] + [f"{SITE}guide/{p.name[:-3]}.html" for p in pages]
        (ROOT / "website" / "sitemap.xml").write_text(
            '<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
            + "".join(f"  <url><loc>{html.escape(u)}</loc></url>\n" for u in urls) + "</urlset>\n", encoding="utf-8")
    print(f"wrote {len(pages)} pages and {len(list(images.glob('*')))} images to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
