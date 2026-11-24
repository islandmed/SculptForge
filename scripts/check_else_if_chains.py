#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""MSVC nests every `else if` one block deeper and stops at about 128 (C1061, "blocks nested too deeply"). GCC and Clang
don't, so a long chain compiles on Linux and breaks the Windows stage (0.12.13: UiTest.cpp's step dispatch, twice).
Fails, naming file:line and the count, on any if / else if chain under src/ with LIMIT (100) or more `if`s.
Use a table or a switch for those (UiTest.cpp's StepVerb).

  scripts/check_else_if_chains.py [repo root]              fail at 100 links
  scripts/check_else_if_chains.py --report 60 [repo root]  also list every chain of 60 or more (follow-ups, exit code unchanged)
  scripts/check_else_if_chains.py --self-test
"""
import os
import re
import sys

LIMIT = 100  # MSVC's 128 blocks, minus what the function, class and namespace already nest
EXTS = (".cpp", ".h", ".mm", ".m", ".c", ".hpp", ".cc", ".cxx", ".inl")
TOKEN = re.compile(r"[A-Za-z_]\w*|\S")


def blank(text):
    """Comments, string and char literals (raw strings too) as spaces, newlines kept: only code is left."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        two = text[i:i + 2]
        if two == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif two == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c == '"' or c == "'":
            raw = c == '"' and re.search(r"R$", text[max(0, i - 1):i]) is not None
            if raw:
                m = re.compile(r'"([^()\\ ]{0,16})\(').match(text, i)
                j = text.find(")" + m.group(1) + '"', m.end()) if m else -1
                j = n if j < 0 else j + len(m.group(1)) + 2
            else:
                j = i + 1
                while j < n and text[j] != c and text[j] != "\n":
                    j += 2 if text[j] == "\\" else 1
                j = min(j + 1, n)
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


class Chains:
    def __init__(self, code):
        self.toks = [(m.group(0), m.start()) for m in TOKEN.finditer(code)]
        self.code = code

    def balanced(self, i, open_, close):  # toks[i] is open_; the index after its match
        depth = 0
        while i < len(self.toks):
            t = self.toks[i][0]
            depth += t == open_
            depth -= t == close
            i += 1
            if depth == 0:
                return i
        return i

    def statement(self, i):  # the index after the statement starting at toks[i]
        toks = self.toks
        if i >= len(toks):
            return i
        t = toks[i][0]
        if t == "{":
            return self.balanced(i, "{", "}")
        if t == "if":
            return self.chain(i)[1]
        if t in ("for", "while", "switch") and i + 1 < len(toks) and toks[i + 1][0] == "(":
            return self.statement(self.balanced(i + 1, "(", ")"))
        if t == "do":
            j = self.statement(i + 1)  # while ( ... ) ;
            return self.statement_end(self.balanced(j + 1, "(", ")")) if j < len(toks) else j
        return self.statement_end(i)

    def statement_end(self, i):  # past the next ';' outside any bracket (lambda bodies and calls stay inside)
        depth = 0
        while i < len(self.toks):
            t = self.toks[i][0]
            if t in "({[":
                depth += 1
            elif t in ")}]":
                depth -= 1
            elif t == ";" and depth <= 0:
                return i + 1
            i += 1
        return i

    def chain(self, i):  # toks[i] is an `if` that starts a chain: (its `if` count, the index after it)
        toks = self.toks
        count = 1
        while True:
            j = i + 1
            if j < len(toks) and toks[j][0] in ("constexpr", "consteval"):
                j += 1
            if j >= len(toks) or toks[j][0] != "(":
                return count, j
            j = self.statement(self.balanced(j, "(", ")"))
            if j < len(toks) and toks[j][0] == "else":
                if j + 1 < len(toks) and toks[j + 1][0] == "if":
                    count += 1
                    i = j + 1
                    continue
                return count, self.statement(j + 1)
            return count, j

    def all(self):  # (line, count) of every chain, by its first `if`
        out = []
        toks = self.toks
        for i, (t, pos) in enumerate(toks):
            if t == "if" and not (i and toks[i - 1][0] == "else"):
                out.append((self.code.count("\n", 0, pos) + 1, self.chain(i)[0]))
        return out


def chains_in(text):
    return Chains(blank(text)).all()


def scan(root, minimum):
    found = []
    for d, _, files in os.walk(os.path.join(root, "src")):
        for f in sorted(files):
            if not f.endswith(EXTS):
                continue
            path = os.path.join(d, f)
            with open(path, encoding="utf-8", errors="replace") as fh:
                for line, count in chains_in(fh.read()):
                    if count >= minimum:
                        found.append((os.path.relpath(path, root), line, count))
    return sorted(found)


def self_test():
    def chain_text(n, tail=""):
        return "void f(int v) {\n" + "".join(("  if (v == %d) {\n    g(%d);\n  }" % (i, i)) if i == 0 else
                                             (" else if (v == %d) {\n    g(%d);\n  }" % (i, i)) for i in range(n)) + tail + "\n}\n"
    ok = lambda text: max([c for _, c in chains_in(text)] or [0])
    assert ok(chain_text(99)) == 99 and ok(chain_text(100)) == 100
    assert ok(chain_text(100, " else { h(); }")) == 100
    # the head is on line 2 and an unbraced chain counts the same
    assert chains_in(chain_text(3))[0] == (2, 3)
    assert ok("void f(){ if (a) x(); else if (b) y(); else if (c) z(); else w(); }") == 3
    # comments and strings don't count; nested chains are their own
    assert ok('void f(){ if (a) { s("else if (b) {"); } // else if (c) {\n /* else if (d) {} */ }') == 1
    assert sorted(c for _, c in chains_in("void f(){ if (a) { if (b) {} else if (c) {} else if (d) {} } else if (e) {} }")) == [2, 3]
    assert ok('void f(){ auto s = R"x(if (a) {} else if (b) {})x"; }') == 0
    # a lambda with its own ifs inside a condition body, and `else` + a different statement, end a chain
    assert ok("void f(){ if (a) { auto l = [&]{ if (q) r(); }; } else if (b) {} else for (;;) { if (c) d(); } }") == 2
    # the scan: a 100-link file fails, 99 passes
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, "src", "app"))
        with open(os.path.join(d, "src", "app", "A.cpp"), "w") as fh:
            fh.write(chain_text(99))
        assert scan(d, LIMIT) == []
        with open(os.path.join(d, "src", "app", "B.cpp"), "w") as fh:
            fh.write(chain_text(100))
        assert scan(d, LIMIT) == [(os.path.join("src", "app", "B.cpp"), 2, 100)]
    print("check_else_if_chains self-test passed")


def main(argv):
    if "--self-test" in argv:
        self_test()
        return 0
    report = None
    if "--report" in argv:
        i = argv.index("--report")
        report = int(argv[i + 1])
        del argv[i:i + 2]
    root = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    found = scan(root, min(LIMIT, report) if report else LIMIT)
    bad = [f for f in found if f[2] >= LIMIT]
    for path, line, count in found:
        tag = "FAIL" if count >= LIMIT else "note"
        print(f"{tag} {path}:{line}: else-if chain of {count} (MSVC stops at about 128 nested blocks, C1061; limit {LIMIT})")
    if bad:
        print("Use a table or a switch (see StepVerb in src/app/UiTest.cpp).")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
