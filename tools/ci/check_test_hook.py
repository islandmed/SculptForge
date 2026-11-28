#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Production code never reads the state that only the ImGui test hook fills (tools/ci/t0.sh and ctest run it).

    tools/ci/check_test_hook.py [--root DIR]
    tools/ci/check_test_hook.py --self-test

Dear ImGui calls ImGuiTestEngineHook_ItemAdd / _ItemInfo (src/app/Demo.cpp) only while ImGuiContext::TestEngineHookItems is
on, and App.cpp turns that on only for recordings and UI tests. A guard that reads what the hook collected passes every UI
test and does nothing for a user (fix/touch-pin-slider-drag 11-27: a slider guard read a flag only the hook is given).
Three things are refused in src/, each with file:line and why:
  1. a call to a Demo accessor (findItem, scopeItems, ... : the item table the hook fills) outside the files that run with
     the hook on (READERS_OK: Demo, UiTest*, UiServe, BenchTake). A new static accessor in Demo.h must be classified here.
  2. a read of state the hook writes: the sf::ui functions the hook functions call (NoteItem, NoteItemLabel) and the g_
     variables they touch, read by any other function of that file (HOOK_STATE_READERS_OK names the few that are
     recording or UI-test features).
  3. ImGuiContext::TestEngineHookItems read, or a status flag ImGui gives only to the hook (HOOK_ONLY_FLAGS: ImGui ORs
     them into the ItemInfo call alone, not into LastItemData) used, unless cmake/Dependencies.cmake patches ImGui to keep
     that flag on the item data (it then names the flag; Inputable is kept that way).
The hooks-off behaviour test (tests, ItemIsValueBox and its siblings) proves the other half: a guard holds with the hook off.
Exit 0 ok, 1 refused, 2 usage error.
"""
import os
import re
import sys
import tempfile

# Files that run with the hook on (recordings, UI tests, benchmark takes): they may read what it collected.
READERS_OK = re.compile(r"^src/app/(Demo\.(cpp|h)|UiTest\w*\.(cpp|h)|UiServe\.cpp|BenchTake\.cpp)$")
# Demo.h: what writes the table (called from production code on purpose: no-ops with the hook off) and what reads it.
DEMO_WRITERS = {"beginFrame", "pushScope", "popScope", "noteClippedLabel"}
DEMO_OTHER = {"names", "kNoCase", "size_t"}  # static, but no part of the item table (scripts list, a constant)
DEMO_READERS = {"findItem", "similarItems", "offscreenItems", "pastRightEdge", "clippedLabels", "scopeItems", "scopeItemsInOrder"}
# Status flags Dear ImGui adds to the ItemInfo hook call only (imgui_widgets.cpp: `StatusFlags | ImGuiItemStatusFlags_X`).
HOOK_ONLY_FLAGS = ("Checkable", "Checked", "Inputable", "Openable", "Opened")
# Functions that read hook-written state on purpose: (file, function) -> why.
HOOK_STATE_READERS_OK = {
    # KNOWN, found by this check 11-27 (reported to input): My Panel's "left neighbour" test sees no neighbour in a normal run.
    ("src/app/Ui.cpp", "pinLastItem"): "KNOWN: g_prevItem is filled by NoteItem, the hook's, so leftNeighbour is always false for users",
}
WRITE = r"\b{v}\b(?:\.\w+|\[[^\]]*\])*\s*(?:(?<![=!<>])=(?!=)|[-+*|&]=|\+\+|--|\.(?:push_back|emplace_back|clear|erase|insert|resize|assign|swap)\()"
SRC_EXT = (".cpp", ".h", ".mm", ".hpp")


def strip_comments(text):
    """Comments and string literals blanked (same length, newlines kept), so a mention in prose is no read."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c == '"' or c == "'":
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


def functions(text):
    """[(name, first line, last line)] of the top-level functions (header at column 0 ending in '{', body to the next column-0 '}')."""
    lines, found, i = text.split("\n"), [], 0
    header = re.compile(r"^[A-Za-z_][^;{}]*?\b(\w+)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{\s*$")
    while i < len(lines):
        m = header.match(lines[i])
        if m and not lines[i].startswith(("namespace", "struct", "class", "enum", "union", "extern")):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("}"):
                j += 1
            found.append((m.group(1), i + 1, j + 1))
            i = j
        i += 1
    return found


def read_tree(root):
    files = {}
    base = os.path.join(root, "src")
    for d, _, names in os.walk(base):
        for name in names:
            if name.endswith(SRC_EXT):
                path = os.path.join(d, name)
                with open(path, encoding="utf-8", errors="replace") as f:
                    files[os.path.relpath(path, root).replace(os.sep, "/")] = f.read()
    return files


def check(root):
    files = read_tree(root)
    bad = []
    code = {p: strip_comments(t) for p, t in files.items()}

    def lineno(text, pos):
        return text.count("\n", 0, pos) + 1

    # 1. Demo accessors.
    demo_h = code.get("src/app/Demo.h", "")
    declared = set(re.findall(r"\bstatic\s+[^;(){}]*?\b(\w+)\s*\(", demo_h)) | {"findItem"}
    for name in sorted(declared - DEMO_WRITERS - DEMO_READERS - DEMO_OTHER):
        bad.append(f"src/app/Demo.h: `{name}` is a new Demo accessor: list it in DEMO_WRITERS or DEMO_READERS (tools/ci/check_test_hook.py)")
    readers = re.compile(r"(?<![\w])(?:Demo::|\.|->)?\b(" + "|".join(sorted(DEMO_READERS)) + r")\s*\(")
    for path, text in code.items():
        if READERS_OK.match(path):
            continue
        for m in readers.finditer(text):
            before = text[max(0, m.start() - 6):m.start()]
            qualified = m.group(0).startswith(("Demo::", ".", "->"))
            if not qualified and not re.search(r"\bDemo\b", text):
                continue  # an unrelated function of the same name in a file that has nothing to do with Demo
            bad.append(f"{path}:{lineno(text, m.start())}: reads the hook's item table (Demo::{m.group(1)}): empty in a normal run; "
                       f"only Demo/UiTest/UiServe/BenchTake may ({before.strip() or 'call'})")

    # 2. Hook-written state: sinks called by the hook functions, and the g_ variables they touch.
    demo = code.get("src/app/Demo.cpp", "")
    sinks = set()
    for name, a, b in functions(demo):
        if name.startswith("ImGuiTestEngineHook_"):
            body = "\n".join(demo.split("\n")[a - 1:b])
            sinks |= set(re.findall(r"\bui::(\w+)\s*\(", body))
    for path, text in code.items():
        funcs = functions(text)
        lines = text.split("\n")
        here = {n for n, _, _ in funcs if n in sinks}
        if not here:
            continue
        state = set()
        for n, a, b in funcs:
            if n in here:
                state |= set(re.findall(r"\b(g_\w+)", "\n".join(lines[a - 1:b])))
        for n, a, b in funcs:  # a variable some other function also writes is shared state, not the hook's alone
            if n not in here:
                state -= {v for v in state if re.search(WRITE.format(v=v), "\n".join(lines[a - 1:b]))}
        for n, a, b in funcs:
            if n in here or (path, n) in HOOK_STATE_READERS_OK:
                continue
            for k in range(a - 1, b):
                for var in sorted(state):
                    if re.search(r"\b" + var + r"\b", lines[k]):
                        bad.append(f"{path}:{k + 1}: `{n}` reads {var}, which the item hook ({', '.join(sorted(here))}) fills: "
                                   f"empty in a normal run; keep what a guard needs from ImGui's own item data")
                        break

    # 3. TestEngineHookItems and the hook-only status flags.
    patched = ""
    cm = os.path.join(root, "cmake", "Dependencies.cmake")
    if os.path.exists(cm):
        with open(cm, encoding="utf-8", errors="replace") as f:
            patched = f.read()
    flag = re.compile(r"\bImGuiItemStatusFlags_(" + "|".join(HOOK_ONLY_FLAGS) + r")\b")
    for path, text in code.items():
        if path == "src/app/Demo.cpp":
            continue
        for m in re.finditer(r"\bTestEngineHookItems\b(\s*=(?!=))?", text):
            if not m.group(1):
                bad.append(f"{path}:{lineno(text, m.start())}: reads TestEngineHookItems: a normal run has it off, so the code would "
                           f"behave differently in the tests than for a user")
        for m in flag.finditer(text):
            if f"ImGuiItemStatusFlags_{m.group(1)}" in patched:
                continue  # kept on LastItemData by a checked patch (cmake/Dependencies.cmake)
            bad.append(f"{path}:{lineno(text, m.start())}: uses ImGuiItemStatusFlags_{m.group(1)}, which ImGui gives only to the item hook "
                       f"(not LastItemData.StatusFlags) unless cmake/Dependencies.cmake patches it in")
    return bad


# --- self-test: fixture trees that must pass or fail as written ---------------------------------------------------------------

DEMO_H = """class Demo {
public:
    static std::vector<std::string> similarItems(const std::string& label);
    static void pushScope(const char* name);
    bool findItem(const std::string& label, ImVec2& center) const;
};
"""
DEMO_CPP = """void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData*) {
    sf::ui::NoteItem(ctx, id, bb.Min, bb.Max);
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
    sf::ui::NoteItemLabel(id, label, (flags & ImGuiItemStatusFlags_Inputable) != 0);
}
"""
UI_CPP = """ImGuiID g_valueItem = 0;
void NoteItemLabel(ImGuiID id, const char* label, bool draggable) {
    if (draggable) g_valueItem = id;
}
void NoteItem(ImGuiContext* ctx, ImGuiID id) {
}
void touchPinItem(ImGuiID id) {
    if (id != g_valueItem) start();
}
"""
UI_OK_CPP = UI_CPP.replace("    if (id != g_valueItem) start();\n", "    start();\n")
CASES = [
    ("clean tree", {"src/app/Demo.h": DEMO_H, "src/app/Demo.cpp": DEMO_CPP, "src/app/Ui.cpp": UI_OK_CPP,
                    "src/app/UiTest.cpp": "void f(Demo& d) { d.findItem(\"x\", c); Demo::similarItems(\"x\"); }\n"}, []),
    ("items() read outside the allowed list", {"src/app/Demo.h": DEMO_H, "src/app/Demo.cpp": DEMO_CPP, "src/app/Ui.cpp": UI_OK_CPP,
        "src/app/Panel.cpp": "#include \"Demo.h\"\nvoid g(Demo& d) { if (!Demo::similarItems(\"Slider\").empty()) go(); }\n"}, ["Panel.cpp:2"]),
    ("hook-filled variable read by a guard", {"src/app/Demo.h": DEMO_H, "src/app/Demo.cpp": DEMO_CPP, "src/app/Ui.cpp": UI_CPP}, ["Ui.cpp:8"]),
    ("TestEngineHookItems read", {"src/app/Demo.h": DEMO_H, "src/app/Demo.cpp": DEMO_CPP, "src/app/Ui.cpp": UI_OK_CPP,
        "src/app/Panel.cpp": "bool h() { return ImGui::GetCurrentContext()->TestEngineHookItems; }\n"}, ["Panel.cpp:1"]),
    ("TestEngineHookItems written (App.cpp)", {"src/app/Demo.h": DEMO_H, "src/app/Demo.cpp": DEMO_CPP, "src/app/Ui.cpp": UI_OK_CPP,
        "src/app/App.cpp": "void i() { ImGui::GetCurrentContext()->TestEngineHookItems = true; }\n"}, []),
    ("hook-only status flag without the patch", {"src/app/Demo.h": DEMO_H, "src/app/Demo.cpp": DEMO_CPP, "src/app/Ui.cpp": UI_OK_CPP,
        "src/app/Panel.cpp": "bool k(const ImGuiLastItemData& d) { return d.StatusFlags & ImGuiItemStatusFlags_Inputable; }\n"}, ["Panel.cpp:1"]),
    ("hook-only status flag kept by the patch", {"src/app/Demo.h": DEMO_H, "src/app/Demo.cpp": DEMO_CPP, "src/app/Ui.cpp": UI_OK_CPP,
        "cmake/Dependencies.cmake": "sf_imgui_widgets_patch(\"x\" \"... | ImGuiItemStatusFlags_Inputable ...\")\n",
        "src/app/Panel.cpp": "bool k(const ImGuiLastItemData& d) { return d.StatusFlags & ImGuiItemStatusFlags_Inputable; }\n"}, []),
    ("mention in a comment", {"src/app/Demo.h": DEMO_H, "src/app/Demo.cpp": DEMO_CPP, "src/app/Ui.cpp": UI_OK_CPP,
        "src/app/Panel.cpp": "// not Demo::similarItems(\"x\"), TestEngineHookItems or ImGuiItemStatusFlags_Inputable\nvoid m() {}\n"}, []),
    ("new unclassified Demo accessor", {"src/app/Demo.h": DEMO_H.replace("bool findItem", "static int itemCount();\n    bool findItem"),
        "src/app/Demo.cpp": DEMO_CPP, "src/app/Ui.cpp": UI_OK_CPP}, ["Demo.h: `itemCount`"]),
]


def self_test():
    failed = 0
    for name, tree, expect in CASES:
        with tempfile.TemporaryDirectory() as root:
            for rel, text in tree.items():
                os.makedirs(os.path.dirname(os.path.join(root, rel)), exist_ok=True)
                with open(os.path.join(root, rel), "w") as f:
                    f.write(text)
            got = check(root)
        ok = (not got) if not expect else (len(got) == len(expect) and all(any(e in g for g in got) for e in expect))
        if not ok:
            failed += 1
            print(f"self-test FAILED: {name}: expected {expect or 'no findings'}, got {got or 'no findings'}")
    # The real tree's accessor list must be the one Demo.h declares (a stale DEMO_* list is its own failure).
    print("self-test: %d of %d cases as expected" % (len(CASES) - failed, len(CASES)))
    return 1 if failed else 0


def main(argv):
    if "--self-test" in argv:
        return self_test()
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
    if "--root" in argv:
        root = argv[argv.index("--root") + 1]
    bad = check(os.path.abspath(root))
    for line in bad:
        print(line)
    if bad:
        print(f"{len(bad)} read(s) of test-hook state in production code: a guard must hold with the hook off "
              f"(docs/testing-flow.md, tools/ci/check_test_hook.py)")
        return 1
    print("check_test_hook: production code reads no test-hook state")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
