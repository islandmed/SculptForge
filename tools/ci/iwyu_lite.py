#!/usr/bin/env python3
"""IWYU-lite: do the files compile with no precompiled header, on the packages' standard library?

Every Linux build target precompiles the standard library headers (cmake/BuildSpeed.cmake), so a file that uses
std::memcmp without <cstring> compiles on every runner that builds with the PCH (CI, T0: ubuntu-24.04, ccache 4.9) and
breaks only where the PCH is off: the Linux packages on Ubuntu 22.04 (ccache 4.6 < 4.8: the PCH switches itself off,
GCC 11, libstdc++ 11), found at the 0.13.11 stage (tests/test_texture.cpp). This syntax-checks the given files
(or all) from build/compile_commands.json without the PCH, with the checking library on (-D_GLIBCXX_ASSERTIONS), and
the packages' library headers when --cxx names their compiler.

  tools/ci/iwyu_lite.py --build build [--cxx g++-11] [-j N] --files src/core/Texture.cpp tests/test_texture.cpp ...
  tools/ci/iwyu_lite.py --build build --all         every src/ and tests/ .cpp
A changed header checks the .cpp files that include it by name (at most --max-fanout of them, then the first ones).
Exit 0 clean, 1 with the files and their first errors, 2 no compile database.
"""
import argparse, concurrent.futures as cf, json, os, shlex, subprocess, sys

DROP_WITH_ARG = {"-o", "-MF", "-MT", "-MQ", "-include"}
DROP = {"-c", "-MD", "-MMD", "-MP", "-Winvalid-pch", "-gsplit-dwarf", "-g", "-flto", "-flto=auto"}


def flags(command, cxx):
    """The compile line without the object, dependency files, PCH and debug info, as a syntax-only check."""
    argv = shlex.split(command)
    if os.path.basename(argv[0]) == "ccache":  # (a compiler launcher in front of it)
        argv = argv[1:]
    out, skip = [cxx or argv[0]], False
    for i, a in enumerate(argv[1:], 1):
        if skip:
            skip = False
        elif a in DROP_WITH_ARG:
            skip = True
        elif a in DROP or a.startswith("-flto"):
            pass
        elif i == len(argv) - 1 and not a.startswith("-"):
            pass  # the source file
        else:
            out.append(a)
    # -w: only errors (missing declarations); the warnings are the build's own business.
    return out + ["-fsyntax-only", "-w", "-D_GLIBCXX_ASSERTIONS"]


def check(argv, source):
    try:
        r = subprocess.run(argv + ["-x", "c++", source], capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return source, "timeout"
    if r.returncode == 0:
        return source, ""
    errs = [l for l in r.stderr.splitlines() if "error" in l or "note: 'std::" in l or "suggested alternative" in l]
    return source, "\n".join(errs[:6] or r.stderr.splitlines()[:6])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build")
    ap.add_argument("--cxx", help="compiler to use instead of the build's (g++-11: the packages' library headers)")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--max-fanout", type=int, default=200, help="most .cpp files one run checks for changed headers")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--files", nargs="*", default=[])
    a = ap.parse_args()
    db = os.path.join(a.build, "compile_commands.json")
    if not os.path.exists(db):
        print(f"iwyu_lite: no {db} (configure first)", file=sys.stderr)
        return 2
    entries = {}
    for e in json.load(open(db)):
        f = os.path.realpath(os.path.join(e["directory"], e["file"]))
        entries[f] = e.get("command") or shlex.join(e["arguments"])
    top = os.path.commonpath([f for f in entries if "/src/" in f]).rsplit("/src", 1)[0]
    wanted = a.files
    if a.all:
        wanted = [os.path.relpath(f, top) for f in entries if f.startswith((top + "/src/", top + "/tests/"))]
    jobs = []
    headers = [r for r in wanted if r.endswith((".h", ".hpp")) and r.startswith(("src/", "tests/"))]
    sources = [r for r in wanted if r.endswith((".cpp", ".cc")) and r.startswith(("src/", "tests/"))]
    if headers:  # the .cpp files that name a changed header in an #include (one level: its includers' own includers rarely lack it)
        names = {os.path.basename(h) for h in headers}
        more = []
        for f in sorted(entries):
            if not f.startswith((top + "/src/", top + "/tests/")):
                continue
            try:
                text = open(f, errors="replace").read()
            except OSError:
                continue
            if any(f'/{n}"' in text or f'"{n}"' in text for n in names):
                more.append(os.path.relpath(f, top))
        if len(more) > a.max_fanout:
            print(f"iwyu_lite: {len(headers)} header(s) reach {len(more)} .cpp files: checking the first {a.max_fanout}")
        sources += more[:a.max_fanout]
    for rel in sorted(set(sources)):
        path = os.path.join(top, rel)
        if path in entries and os.path.exists(path):  # (not deleted by the change)
            jobs.append((flags(entries[path], a.cxx), path))
    if not jobs:
        print("iwyu_lite: no C++ files to check")
        return 0
    bad = []
    with cf.ThreadPoolExecutor(a.j) as ex:
        for src, err in ex.map(lambda j: check(*j), jobs):
            if err:
                bad.append((os.path.relpath(src, top), err))
    print(f"iwyu_lite: {len(jobs)} file(s) without the PCH ({a.cxx or 'build compiler'}, _GLIBCXX_ASSERTIONS): {len(bad)} failed")
    for f, err in bad:
        print(f"\n{f}:\n{err}")
    if bad:
        print("\nThe PCH (cmake/BuildSpeed.cmake) hides a missing standard include on CI and T0; the Ubuntu 22.04 packages "
              "build without it. Add the #include the file uses (std::memcmp: <cstring>).")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
