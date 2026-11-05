#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Translation coverage (src/app/I18n.h): every interface string the code hands to the translator, and per language
which of them the table has no translation for.

    tools/i18n/coverage.py              counts and percent per language
    tools/i18n/coverage.py --missing    also every missing string (es, zh-TW, or --lang es)
    tools/i18n/coverage.py --check      exit 1 unless every language is at 100% (ctest i18n_coverage)
    tools/i18n/coverage.py --json       the strings and what each language lacks, for scripts

The strings are found in src/app/*.cpp, the label paths tr() is used through:
  - a literal passed to tr(), trPath() (each "A > B" step) or a wrapper that calls tr() on it (WRAPPERS below:
    ui::Checkbox, ui::HelpText, ui::DelayedTooltip, ui::SliderInside formats, hint(), subpalette()...);
  - actions (trAction, the Ctrl-help box, the command palette): label (not "brush.*": brushes keep their names),
    description and palette path, from `x.label = "..."` assignments, `add({...})` and the files' own add() lambdas;
  - the tables TABLES names (palettes, task menus, Preferences > Hints).
A string counts as translated the way tr() finds it: an exact entry, or one for the words without the icon in front,
the "##id" behind, a trailing "..." or ":" or printf tail ("Size %.0f" from "Size").
Strings that stay English everywhere (product names, units, file formats) go in tools/i18n/keep_english.txt.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "src" / "app"
KEEP = Path(__file__).resolve().parent / "keep_english.txt"
LANGS = {"es": SRC / "I18nSpanish.cpp", "zh-TW": SRC / "I18nTraditionalChinese.cpp"}
SKIP_FILES = {"I18n.cpp", "I18nSpanish.cpp", "I18nTraditionalChinese.cpp"}

# tr() and trPath() translate their argument; every function or lambda that passes one of its parameters on to them
# (or to another such function) is found in the sources (wrappers()): ui::Checkbox, ui::HelpText, hint()... A name
# qualified with ImGui:: is ImGui's own and skipped.
BASE = {"tr": {0}, "trPath": {0}}
# What the search can't see: tray::SliderInt hands ui::SliderInside a copy of its format; the help box shows
# ui::HelpText's path through trPath (Ui.cpp).
EXTRA = {"SliderInt": {4}, "HelpText": {3}}
PATHS = {"trPath": {0}, "HelpText": {3}}  # arguments that are "A > B" paths
NOT_FUNCTIONS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "decltype", "alignof", "defined"}
# Struct tables whose fields are shown through tr(): table name -> field indices (a nested {...} field: each literal).
TABLES = {"kPalettes": [1], "kMenus": [1, 3], "kHints": [1]}
ACTION_FIELDS = ("label", "description", "palette")

ICON = "\x00ICON\x00"  # stands for an ICON_LC_* macro inside a label
TOKEN = re.compile(
    r'//[^\n]*|/\*.*?\*/'                           # comments
    r'|(?:u8|u|U|L)?R"([^(\s]*)\((.*?)\)\1"'         # raw strings
    r'|(?:u8|u|U|L)?"((?:[^"\\\n]|\\.)*)"'           # strings
    r"|'(?:[^'\\\n]|\\.)*'"                          # chars
    r'|[A-Za-z_][A-Za-z_0-9]*|::|->|\d[\w.]*|\S', re.S)


def unescape(s):
    out, i = [], 0
    while i < len(s):
        c = s[i]
        if c != "\\":
            out.append(c)
            i += 1
            continue
        n = s[i + 1]
        i += 2
        if n in "ntr0\\\"'?":
            out.append({"n": "\n", "t": "\t", "r": "\r", "0": "\0"}.get(n, n))
        elif n == "x":
            m = re.match(r"[0-9a-fA-F]+", s[i:])
            out.append(chr(int(m.group(), 16)))
            i += len(m.group())
        elif n in "uU":
            k = 4 if n == "u" else 8
            out.append(chr(int(s[i:i + k], 16)))
            i += k
        else:
            out.append(n)
    return "".join(out)


def tokens(text):
    """(kind, value, line): kind is 'str' (adjacent literals and ICON_LC_* macros joined), 'id' or 'p'."""
    out = []
    for m in TOKEN.finditer(text):
        t = m.group(0)
        line = text.count("\n", 0, m.start()) + 1
        if t.startswith("//") or t.startswith("/*"):
            continue
        if m.group(2) is not None or m.group(3) is not None:
            v = m.group(2) if m.group(2) is not None else unescape(m.group(3))
            if out and out[-1][0] == "str":
                out[-1] = ("str", out[-1][1] + v, out[-1][2])
            elif out and out[-1][0] == "id" and out[-1][1].startswith("ICON_"):
                out[-1] = ("str", ICON + " " + v.lstrip(" "), out[-1][2])
            else:
                out.append(("str", v, line))
        elif re.match(r"[A-Za-z_]", t):
            if t.startswith("ICON_") and out and out[-1][0] == "str":
                out[-1] = ("str", out[-1][1] + ICON, out[-1][2])
            else:
                out.append(("id", t, line))
        else:
            out.append(("p", t, line))
    return out


def split_args(toks, i):
    """toks[i] is '(' or '{': the arguments up to the matching bracket, as token lists, and the index after it."""
    close = {"(": ")", "{": "}", "[": "]"}
    depth, args, cur = 0, [], []
    j = i
    while j < len(toks):
        k, v, _ = toks[j]
        if k == "p" and v in close:
            depth += 1
            if depth == 1:
                j += 1
                continue
        elif k == "p" and v in close.values():
            depth -= 1
            if depth == 0:
                args.append(cur)
                return [a for a in args if a] if args != [[]] else [], j + 1
        elif k == "p" and v == "," and depth == 1:
            args.append(cur)
            cur = []
            j += 1
            continue
        cur.append(toks[j])
        j += 1
    return args, j


def literal(arg):
    """The argument's string when it is a plain literal (or std::string("...")), else None."""
    if len(arg) == 1 and arg[0][0] == "str":
        return arg[0][1]
    vals = [t for t in arg if t[0] == "str"]
    if len(vals) == 1 and [t[1] for t in arg if t[0] != "str"] in (["std", "::", "string", "(", ")"], ["(", ")"]):
        return vals[0][1]
    return None


def literals_in(arg):
    """Every literal in an argument: both sides of a ternary, a nested {...} list."""
    return [t[1] for t in arg if t[0] == "str"]


def definitions(toks):
    """(name, parameter names, body start, body end) of the functions and named lambdas defined in a token list."""
    out = []
    for i, (k, v, _) in enumerate(toks):
        if k != "id" or i + 1 >= len(toks) or toks[i + 1][1] not in ("(", "="):
            continue
        if toks[i + 1][1] == "=":  # auto name = [...](params) {
            if not (i and toks[i - 1][1] == "auto" and i + 2 < len(toks) and toks[i + 2][1] == "["):
                continue
            j = i + 2
            while j < len(toks) and toks[j][1] not in ("(", "{", ";"):
                j += 1
            if j >= len(toks) or toks[j][1] != "(":
                continue
        else:
            if v in NOT_FUNCTIONS or (i and toks[i - 1][1] in (".", "->")):
                continue
            j = i + 1
        params, end = split_args(toks, j)
        while end < len(toks) and toks[end][1] in ("const", "noexcept", "override", "mutable", "->", "bool", "void"):
            end += 1
        if end >= len(toks) or toks[end][1] != "{":
            continue
        if not params or not all(len(p) >= 2 and p[-1][0] == "id" or "=" in [t[1] for t in p] for p in params):
            continue
        names = [(p[:[t[1] for t in p].index("=")] if "=" in [t[1] for t in p] else p)[-1][1] for p in params]
        _, body_end = split_args(toks, end)
        out.append((v, names, end, body_end, [[t[1] for t in p] for p in params]))
    return out


def translated_params(toks, defs, known):
    """name -> parameter indices that reach tr(), given the wrappers already known."""
    found = {}
    for name, params, start, end, _ in defs:
        hit = set()
        for i in range(start, end):
            k, v, _ = toks[i]
            if k != "id" or i + 1 >= end or toks[i + 1][1] != "(" or toks[i - 1][1] in (".", "->"):
                continue
            if toks[i - 1][1] == "::" and toks[i - 2][1] == "ImGui":
                continue
            idx = known.get(v)
            if not idx:
                continue
            args, _ = split_args(toks, i + 1)
            for a in idx:
                if a < len(args):
                    words = [t[1] for t in args[a] if t[1] not in ("std", "::", "string", "(", ")", ".", "c_str")]
                    if len(words) == 1 and words[0] in params:
                        hit.add(params.index(words[0]))
        if hit and name not in BASE:
            found.setdefault(name, set()).update(hit)
    return found


def wrappers(files):
    """The translating functions: global ones (defined in any file) and each file's own (lambdas, local helpers)."""
    toks = {f: tokens(f.read_text(encoding="utf-8")) for f in files}
    defs = {f: definitions(t) for f, t in toks.items()}
    glob = {n: set(i) for n, i in {**BASE, **EXTRA}.items()}
    local = {f: {} for f in files}
    for _ in range(4):  # wrappers of wrappers
        for f in files:
            known = {**glob, **local[f]}
            for name, idx in translated_params(toks[f], defs[f], known).items():
                local[f].setdefault(name, set()).update(idx)
        glob = {n: set(i) for n, i in {**BASE, **EXTRA}.items()}
        for f in files:
            for name, idx in local[f].items():
                glob.setdefault(name, set()).update(idx)
    # A name the file defines itself means its own definition there (PanelFaceGroups' group() runs a command).
    calls = {}
    for f in files:
        mine = {d[0] for d in defs[f]}
        calls[f] = {n: i for n, i in glob.items() if n not in mine or n in BASE}
        calls[f].update(local[f])
    return toks, calls


def scan_file(path, found, toks, calls):
    name = path.name

    def add(s, where, kind="label"):
        if s is None:
            return
        for part in (s.split(" > ") if kind == "path" else [s]):
            if part.strip():
                found.setdefault(part, set()).add(f"{name}:{where}")

    def add_action(args, line):  # an Action's fields in order: id, label, icon, description, palette, ...
        if len(args) >= 5:
            if not (literal(args[0]) or "").startswith("brush."):
                add(literal(args[1]), line)
            add(literal(args[3]), line)
            add(literal(args[4]), line, "path")

    # The file's own action-adding helpers: auto add = [...](const char* id, const char* label, ...) { actions_.add(a) }
    # (the nearest definition before a call: a file can define two add lambdas).
    lambdas = {}
    for fname, params, start, end, types in definitions(toks):
        body = [t[1] for t in toks[start:end]]
        adds = any(body[j:j + 3] == ["actions_", ".", "add"] for j in range(len(body)))
        f = None
        if adds and "label" in params:
            f = {fld: params.index(fld) for fld in ACTION_FIELDS if fld in params}
            f["id"] = params.index("id") if "id" in params else -1
        elif adds and len(types) == 1 and "Action" in types[0]:
            f = "struct"  # add({"id", "Label", icon, "description", "palette", ...}): an Action as it is
        lambdas.setdefault(fname, []).append((start, f))

    for i, (k, v, line) in enumerate(toks):
        nxt = toks[i + 1][1] if i + 1 < len(toks) else ""
        prev = toks[i - 1][1] if i else ""
        prev2 = toks[i - 2][1] if i > 1 else ""
        if k == "id" and nxt == "(" and prev not in (".", "->") and not (prev == "::" and prev2 == "ImGui"):
            if v in calls:
                args, _ = split_args(toks, i + 1)
                for a in sorted(calls[v]):
                    if a < len(args):
                        for s in (literals_in(args[a]) if v == "tr" else [literal(args[a])]):
                            add(s, line, "path" if a in PATHS.get(v, ()) else "label")
            elif v in lambdas and any(start < i and f for start, f in lambdas[v]) \
                    and [f for start, f in lambdas[v] if start < i][-1]:
                f = [f for start, f in lambdas[v] if start < i][-1]
                if f == "struct":
                    if toks[i + 2][1] == "{":
                        add_action(split_args(toks, i + 2)[0], line)
                    continue
                args, _ = split_args(toks, i + 1)
                aid = literal(args[f["id"]]) if 0 <= f["id"] < len(args) else None
                for field in ACTION_FIELDS:
                    if field in f and f[field] < len(args):
                        if field == "label" and aid and aid.startswith("brush."):
                            continue
                        add(literal(args[f[field]]), line, "path" if field == "palette" else "label")
        # actions_.add({"id", "Label", icon, "description", "palette", ...})
        if k == "id" and v == "add" and prev == "." and nxt == "(" and i + 2 < len(toks) and toks[i + 2][1] == "{" \
                and toks[i - 2][1] in ("actions_", "reg"):
            add_action(split_args(toks, i + 2)[0], line)
        # a.label = "..."; a.description = "..."; a.palette = "..."
        if k == "id" and v in ACTION_FIELDS and prev in (".", "->") and nxt == "=":
            j, val = i + 2, []
            while j < len(toks) and toks[j][1] != ";":
                val.append(toks[j])
                j += 1
            obj = toks[i - 2][1]
            idv = None
            if v == "label":  # skip brush labels: the action's id is set just before
                for b in range(i - 1, max(0, i - 60), -1):
                    if toks[b][1] == "id" and toks[b - 1][1] == "." and toks[b - 2][1] == obj and toks[b + 1][1] == "=":
                        idv = literal([toks[b + 2]]) if toks[b + 3][1] == ";" else None
                        break
            if not (idv and idv.startswith("brush.")) and obj != "g_help":
                add(literal(val), line, "path" if v == "palette" else "label")
        if k == "id" and v in TABLES and nxt == "[":
            j = i
            while j < len(toks) and toks[j][1] != "{":
                j += 1
            rows, _ = split_args(toks, j)
            for row in rows:
                if row and row[0][1] == "{":
                    fields, _ = split_args(row, 0)
                    for f in TABLES[v]:
                        if f < len(fields):
                            for s in literals_in(fields[f]):
                                add(s, line)


def collect():
    found = {}
    files = [p for p in sorted(SRC.glob("*.cpp")) if p.name not in SKIP_FILES]
    toks, calls = wrappers(files)
    for path in files:
        scan_file(path, found, toks[path], calls[path])
    # Not words: printf formats alone, ids, punctuation, numbers.
    return {s: w for s, w in found.items() if words(s)}


def words(s):
    core = visible(s)
    core = re.sub(r"%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z%]", "", core)
    return re.search(r"[A-Za-z]{2,}", core) is not None


def visible(s):
    """The words tr() translates: without the icons in front (and the spaces after them) and the "##id" behind."""
    while s.startswith(ICON):
        s = s[len(ICON):].lstrip(" ")
    return s.split("##")[0]


def load_table(path):
    toks = tokens(path.read_text(encoding="utf-8"))
    table = {}
    for i in range(len(toks) - 4):
        if toks[i][1] == "{" and toks[i + 1][0] == "str" and toks[i + 2][1] == "," and toks[i + 3][0] == "str" \
                and toks[i + 4][1] == "}":
            table[toks[i + 1][1].replace(ICON, "")] = toks[i + 3][1]
    return table


def translates(table, text):
    """tr()'s rule (I18n.cpp compose/translateCore): is there a translation of the label's words?"""
    t = visible(text)

    def core(s):
        if not s:
            return False
        if s in table:
            return True
        if len(s) > 3 and s.endswith("..."):
            return core(s[:-3])
        if len(s) > 1 and s.endswith(":"):
            return core(s[:-1])
        p = s.find("%")
        if p > 0:
            return core(s[:p].rstrip(" "))
        return False
    return core(t)


def load_keep():
    keep = set()
    if KEEP.exists():
        for ln in KEEP.read_text(encoding="utf-8").splitlines():
            if ln.strip() and not ln.startswith("#"):
                keep.add(ln.rstrip("\n"))
    return keep


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--missing", action="store_true", help="list the missing strings")
    ap.add_argument("--lang", choices=list(LANGS), help="only this language")
    ap.add_argument("--check", action="store_true", help="exit 1 unless every language is at 100%%")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    strings = collect()
    keep = load_keep()
    stale = sorted(k for k in keep if not any(visible(s) == k or s == k for s in strings))
    todo = {s: w for s, w in strings.items() if visible(s) not in keep and s not in keep}
    langs = [args.lang] if args.lang else list(LANGS)
    missing = {}
    for lang in langs:
        table = load_table(LANGS[lang])
        missing[lang] = sorted(s for s in todo if not translates(table, s))
    if args.json:
        json.dump({"strings": {s.replace(ICON, "<icon>"): sorted(w) for s, w in strings.items()},
                   "missing": {k: [s.replace(ICON, "<icon>") for s in v] for k, v in missing.items()}},
                  sys.stdout, ensure_ascii=False, indent=1)
        return 0
    n = len(todo)
    print(f"{len(strings)} interface strings, {len(strings) - n} kept in English (keep_english.txt), {n} to translate")
    for lang in langs:
        m = len(missing[lang])
        print(f"  {lang:6} {n - m:5}/{n}  {100.0 * (n - m) / max(n, 1):6.2f}%  ({m} missing)")
        if args.missing or (args.check and m):
            for s in missing[lang]:
                print(f"    {s.replace(ICON, '<icon>')!r}  {min(strings[s])}")
    if stale:
        print(f"keep_english.txt lines no string uses any more: {stale}")
    if args.check and (any(missing.values()) or stale):
        print("i18n_coverage: add the missing strings to the tables (or keep_english.txt for names and units)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
