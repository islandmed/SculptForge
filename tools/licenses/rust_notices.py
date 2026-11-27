#!/usr/bin/env python3
# SculptForge - the notices of the Rust crates inside the Wasmtime C API library
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   python3 -I tools/licenses/rust_notices.py --lock Cargo.lock --cache DIR --out FILE
#
# Wasmtime's release archive carries only Wasmtime's own LICENSE, but its static library holds Rust crates (and the C code of
# ring, zstd and ittnotify), most under MIT or BSD terms that want their notice to travel with the binary. This lists them
# and reproduces their licence files, grouped by identical text.
#
#   Cargo.lock   the lock file at the Wasmtime tag (cmake/Fetch.cmake's version), from github.com/bytecodealliance/wasmtime
#   --cache      where the downloaded .crate files are kept (each is checked against Cargo.lock's SHA-256)
#
# Every crate the lock file reaches from wasmtime-c-api is listed, except NEVER (tests, benchmarks, command-line tools and
# build-only helpers). That is more than the library holds (features, other platforms, proc-macros): leaving a crate out is the
# legal risk, listing one too many only costs pages. Standard library only.
import argparse
import collections
import hashlib
import io
import re
import sys
import tarfile
import tomllib
import urllib.request

ROOT = "wasmtime-c-api"
# crates that are for the tests, the benchmarks, the command line or a build step
NEVER = {"criterion", "criterion-plot", "proptest", "arbtest", "clap", "clap_builder", "clap_derive", "clap_lex",
         "libtest-mimic", "test-log", "test-log-macros", "mutatis", "mutatis-derive", "rusty-fork", "wait-timeout",
         "capstone", "capstone-sys", "xed-sys", "cc", "pkg-config", "find-msvc-tools", "shlex", "jobserver",
         "cargo_metadata", "cargo-platform", "souper-ir"}
LICENSE_FILE = re.compile(r"(^|/)(LICEN[CS]E|COPYING|UNLICENSE|NOTICE)[^/]*$", re.I)


def reachable(lock):
    by = collections.defaultdict(list)
    for p in lock["package"]:
        by[p["name"]].append(p)
    seen, todo = {}, list(by[ROOT])
    while todo:
        p = todo.pop()
        key = (p["name"], p["version"])
        if key in seen:
            continue
        seen[key] = p
        for dep in p.get("dependencies", []):
            parts = dep.split(" ")
            found = by.get(parts[0], [])
            if len(parts) > 1:
                found = [x for x in found if x["version"] == parts[1]]
            todo += found
    return [p for p in seen.values() if "source" in p]


def fetch(p, cache):
    url = "https://static.crates.io/crates/%s/%s-%s.crate" % (p["name"], p["name"], p["version"])
    path = "%s/%s-%s.crate" % (cache, p["name"], p["version"])
    try:
        data = open(path, "rb").read()
    except OSError:
        with urllib.request.urlopen(url, timeout=60) as r:
            data = r.read()
        open(path, "wb").write(data)
    if hashlib.sha256(data).hexdigest() != p["checksum"]:
        sys.exit("checksum of %s-%s differs from Cargo.lock" % (p["name"], p["version"]))
    return data


def read_crate(data):
    meta, files = {}, {}
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as t:
        for m in t.getmembers():
            if not m.isfile():
                continue
            rel = m.name.split("/", 1)[1] if "/" in m.name else m.name
            if rel == "Cargo.toml":
                meta = tomllib.loads(t.extractfile(m).read().decode("utf-8", "replace")).get("package", {})
            elif LICENSE_FILE.search(rel) and rel.count("/") <= 2 and m.size < 200_000:
                files[rel] = t.extractfile(m).read().decode("utf-8", "replace").replace("\r\n", "\n").strip()
    return meta, files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lock", required=True)
    ap.add_argument("--cache", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    lock = tomllib.load(open(a.lock, "rb"))
    crates = sorted((p for p in reachable(lock) if p["name"] not in NEVER), key=lambda p: (p["name"], p["version"]))
    groups = collections.OrderedDict()  # text -> [crate label]
    table = []
    for p in crates:
        meta, files = read_crate(fetch(p, a.cache))
        label = "%s %s" % (p["name"], p["version"])
        lic = meta.get("license") or meta.get("license-file") or "see its repository"
        if not files:  # (fxprof-processed-profile and a few keep them in the repository root, not in the package)
            authors = ", ".join(re.sub(r"\s*<[^>]*>", "", x) for x in meta.get("authors", [])) or "see repository"  # (names, not e-mail addresses)
            lic += " (no licence file in the package; authors: %s; %s)" % (authors, meta.get("repository", ""))
        table.append((label, lic, meta.get("repository", "")))
        for rel in sorted(files):
            groups.setdefault(files[rel], []).append(label)
    with open(a.out, "w") as o:
        o.write("# Notices of the Rust crates in the Wasmtime C API library\n\n")
        o.write("Wasmtime 51.0.0 (Apache-2.0 WITH LLVM-exception, see THIRD_PARTY_NOTICES.md) is linked statically with Rust\n")
        o.write("crates. This lists every crate its lock file reaches from the C API, except those for tests, benchmarks, the command line\n")
        o.write("and build steps; some of them are not in a given build. Each is used under the licence terms its own files state; where a crate offers a choice (for\n")
        o.write("example MIT OR Apache-2.0) SculptForge uses it under the permissive option. Generated by\n")
        o.write("tools/licenses/rust_notices.py from Wasmtime's Cargo.lock at v51.0.0; every crate was downloaded from crates.io and\n")
        o.write("checked against the lock file's SHA-256. The library also holds C code of ring (ISC and OpenSSL-style terms,\n")
        o.write("with BoringSSL parts), zstd (BSD-3-Clause) and Intel's ittnotify (BSD-3-Clause); their licence files are among the texts.\n\n")
        o.write("## Crates\n\n| Crate | Licence (from its Cargo.toml) |\n|---|---|\n")
        for label, lic, repo in table:
            o.write("| %s | %s |\n" % (label, lic.replace("|", "/")))
        mpl = [l for l, lic, _ in table if "MPL" in lic and " OR " not in lic]
        if mpl:  # (file-level copyleft: its source is the crate itself, unmodified)
            o.write("\nMPL-2.0 crates, used unmodified; their source is at https://crates.io/crates/<name>/<version>: %s.\n" % ", ".join(mpl))
        o.write("\nA crate that offers GPL or LGPL next to a permissive licence (ittapi: GPL-2.0-only OR BSD-3-Clause; r-efi: MIT OR Apache-2.0 OR LGPL-2.1-or-later) is used under the permissive one.\n")
        o.write("\n## Licence texts\n\nEach text is given once, with the crates whose package contains it.\n")
        for text, labels in groups.items():
            o.write("\n---\n\n**Used by:** %s\n\n```\n%s\n```\n" % (", ".join(labels), text.replace("```", "'''")))
    print("%d crates, %d distinct texts -> %s" % (len(crates), len(groups), a.out), file=sys.stderr)


main()
