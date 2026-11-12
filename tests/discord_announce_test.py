#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""ctest discord_announce: the release post the workflow builds (.github/workflows/discord-announce.yml, compose and card_url).

The script lives inline in the workflow (it runs with the webhook secret, so it never comes from a checkout), so this pulls
the two functions out of the YAML with ast and runs them: header, headline first and in bold, masked links in angle
brackets (no raw URL lists), the beta's text and link, the 2000-character cap's raw material, and card_url's answers."""
import ast
import pathlib
import sys
import textwrap

ROOT = pathlib.Path(__file__).resolve().parent.parent
YML = (ROOT / ".github" / "workflows" / "discord-announce.yml").read_text(encoding="utf-8")


def fail(msg):
    sys.exit("FAIL: " + msg)


def script():
    lines = YML.split("\n")
    start = next(i for i, l in enumerate(lines) if l.strip() == "python3 - <<'EOF'") + 1
    end = next(i for i in range(start, len(lines)) if lines[i].strip() == "EOF")
    return textwrap.dedent("\n".join(lines[start:end]))


def load(names, **env):
    tree = ast.parse(script())
    funcs = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in names]
    # compose and card_url are defined inside the module body after the payload setup: top level only.
    if {f.name for f in funcs} != set(names):
        fail(f"the workflow's script lacks {sorted(set(names) - {f.name for f in funcs})} at top level")
    g = {"SITE": "https://sculptforge.app", "UA": "test", **env}
    exec(compile(ast.Module(funcs, []), "discord-announce.yml", "exec"), g)
    return g


g = load(["compose", "card_url"])
compose = g["compose"]

# 1. a stable release: header, bold headline, the rest as bullets, one masked link line, no raw URLs
t = compose("0.12.8", "v0.12.8", False, "https://github.com/x/y/releases/tag/v0.12.8",
            ["Reference images: drawings in the view.", "Two-key shortcuts: A then S", "Start-up fix (#192)"])
lines = t.split("\n")
if lines[0] != "## SculptForge 0.12.8":
    fail(f"header: {lines[0]!r}")
if lines[1] != "**Reference images: drawings in the view.**":
    fail(f"headline first and bold: {lines[1]!r}")
if lines[2:4] != ["• Two-key shortcuts: A then S", "• Start-up fix (#192)"]:
    fail(f"bullets: {lines[2:4]!r}")
want = "[Download](<https://sculptforge.app>) · [What's new](<https://sculptforge.app/guide/whats-new.html#version-0128>)"
if lines[-1] != want:
    fail(f"links: {lines[-1]!r}")
import re
if re.search(r"(?<![<(])https?://", t.replace(want, "")):
    fail("a raw URL in the post: " + t)

# 2. no highlights (the feed never named the version): still a header and the links
t = compose("0.12.9", "v0.12.9", False, "u", [])
if t.split("\n")[:2] != ["## SculptForge 0.12.9", "SculptForge 0.12.9 is out."] or "[What's new](<" not in t:
    fail("no highlights: " + t)

# 3. a beta: its own header, the headline, feedback lines, a masked release link, no What's new link
t = compose("0.13.0", "0.13.0 Beta 2", True, "https://github.com/x/y/releases/tag/v0.13.0-beta.2", ["Booleans: cut and join."])
if not t.startswith("## SculptForge 0.13.0 Beta 2 is out for testing\n**Booleans: cut and join.**\n"):
    fail("beta head: " + t)
if "#bug-reports" not in t or not t.endswith("[Download the beta](<https://github.com/x/y/releases/tag/v0.13.0-beta.2>) (Windows, macOS, Linux)"):
    fail("beta tail: " + t)

# 4. a highlight already ending in a period does not get two
if compose("1.0.0", "v1.0.0", False, "u", ["Done."]).split("\n")[1] != "**Done.**":
    fail("double period")

# 5. card_url: a 200 image is the card; a 404, a non-image 200 and a network error are none
class Resp:
    def __init__(self, status, ctype):
        self.status, self.headers = status, {"Content-Type": ctype}

    def __enter__(self):
        return self

    def __exit__(self, *a):
        return False


class Urllib:
    """A stand-in urllib.request: .answer is a Resp or an exception."""
    answer = None
    seen = []

    def Request(self, url, method=None, headers=None):
        self.seen.append((url, method))
        return url

    def urlopen(self, req, timeout=0):
        if isinstance(self.answer, Exception):
            raise self.answer
        return self.answer


u = Urllib()
cg = load(["card_url"], urllib=type("m", (), {"request": u}))
for answer, ok in ((Resp(200, "image/jpeg"), True), (Resp(200, "text/html"), False), (Resp(404, "image/jpeg"), False),
                   (OSError("down"), False)):
    u.answer = answer
    got = cg["card_url"]("0.12.8")
    if bool(got) != ok or (ok and got != "https://sculptforge.app/media/release-0.12.8.jpg"):
        fail(f"card_url for {answer!r}: {got!r}")
if u.seen[0] != ("https://sculptforge.app/media/release-0.12.8.jpg", "HEAD"):
    fail(f"card_url asked {u.seen[0]!r}")
print("discord_announce ok")
