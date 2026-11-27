#!/usr/bin/env python3
# SculptForge - the notices of the Rust crates inside the Wasmtime C API library
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   python3 -I tools/licenses/rust_notices.py --lock Cargo.lock --strings STRINGS.txt --cache DIR --out FILE
#
# Wasmtime's release archive carries only Wasmtime's own LICENSE, but its static library holds about a hundred Rust
# crates (and the C code of ring, zstd and ittnotify), most under MIT or BSD terms that want their notice to travel
# with the binary. This lists them and reproduces their licence files, grouped by identical text.
#
#   Cargo.lock   the lock file at the Wasmtime tag (cmake/Fetch.cmake's version), from github.com/bytecodealliance/wasmtime
#   STRINGS.txt  `strings -n 4 libwasmtime.a` of the Linux x86_64 C API archive: a crate shows up as a name in its symbols
#   --cache      where the downloaded .crate files are kept (each is checked against Cargo.lock's SHA-256)
#
# A crate is listed when (1) the lock file reaches it from wasmtime-c-api, and (2) its name is in the library's symbols,
# or it is a platform crate of the Windows and macOS builds, or it is one of the small crates that compile away without
# a symbol (EXTRA). Test, benchmark, command-line and build-only crates are therefore left out. A crate that
# compiled away with no symbol and is not in EXTRA could be missing; that is why THIRD_PARTY_NOTICES.md also points at the
# lock file. Standard library only.
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
PLATFORM = re.compile(r"^(windows[-_].*|mach2|winapi.*|wasip\d|wasi|libredox|redox_syscall)$")
EXTRA = {"cfg-if", "bitflags", "itoa", "ryu", "num-traits", "subtle", "zeroize", "sha2", "cpufeatures", "crc32fast",
         "adler2", "simd-adler32", "lazy_static", "pin-project-lite", "fnv", "stable_deref_trait", "foldhash",
         "libm", "linux-raw-sys", "errno", "libc", "zstd-sys", "ittapi-sys", "ring", "untrusted", "getrandom"}
# crates that are for the tests, the examples or a build step, even where a symbol of the same name exists
NEVER = {"criterion", "criterion-plot", "proptest", "arbtest", "arbitrary", "derive_arbitrary", "clap", "clap_builder",
         "clap_derive", "clap_lex", "libtest-mimic", "test-log", "test-log-macros", "tempfile", "mutatis",
         "mutatis-derive", "rusty-fork", "wait-timeout", "capstone", "capstone-sys", "xed-sys", "directories",
         "dirs-sys", "option-ext", "wasm-compose", "wasm-wave", "souper-ir", "cc", "pkg-config", "find-msvc-tools",
         "shlex", "jobserver", "cargo_metadata", "cargo-platform", "witx"}
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


def in_symbols(name, text):
    n = name.replace("-", "_")
    return bool(re.search(r"(?<![A-Za-z0-9])%d%s(?![a-z0-9])" % (len(n), re.escape(n)), text) or
                re.search(r"registry/src/[^/]+/%s-" % re.escape(name), text) or
                re.search(r"(?<![A-Za-z0-9_])%s(?![A-Za-z0-9])" % re.escape(n), text))


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
    ap.add_argument("--strings", required=True)
    ap.add_argument("--cache", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    lock = tomllib.load(open(a.lock, "rb"))
    syms = open(a.strings, errors="replace").read()
    crates = sorted((p for p in reachable(lock)
                     if p["name"] not in NEVER and (p["name"] in EXTRA or PLATFORM.match(p["name"]) or in_symbols(p["name"], syms))),
                    key=lambda p: (p["name"], p["version"]))
    groups = collections.OrderedDict()  # text -> [crate label]
    table = []
    for p in crates:
        meta, files = read_crate(fetch(p, a.cache))
        label = "%s %s" % (p["name"], p["version"])
        lic = meta.get("license") or meta.get("license-file") or "see its repository"
        if not files:  # (fxprof-processed-profile and a few keep them in the repository root, not in the package)
            lic += " (no licence file in the package; authors: %s; %s)" % (", ".join(meta.get("authors", [])) or "see repository", meta.get("repository", ""))
        table.append((label, lic, meta.get("repository", "")))
        for rel in sorted(files):
            groups.setdefault(files[rel], []).append(label)
    with open(a.out, "w") as o:
        o.write("# Notices of the Rust crates in the Wasmtime C API library\n\n")
        o.write("Wasmtime 51.0.0 (Apache-2.0 WITH LLVM-exception, see THIRD_PARTY_NOTICES.md) is linked statically with the\n")
        o.write("crates below. Each is used under the licence terms its own files state; where a crate offers a choice (for\n")
        o.write("example MIT OR Apache-2.0) SculptForge uses it under the permissive option. Generated by\n")
        o.write("tools/licenses/rust_notices.py from Wasmtime's Cargo.lock at v51.0.0; every crate was downloaded from crates.io and\n")
        o.write("checked against the lock file's SHA-256. The library also holds C code of ring (ISC and OpenSSL-style terms,\n")
        o.write("with BoringSSL parts), zstd (BSD-3-Clause) and Intel's ittnotify (BSD-3-Clause); their licence files are among the texts.\n\n")
        o.write("## Crates\n\n| Crate | Licence (from its Cargo.toml) |\n|---|---|\n")
        for label, lic, repo in table:
            o.write("| %s | %s |\n" % (label, lic.replace("|", "/")))
        o.write("\n## Licence texts\n\nEach text is given once, with the crates whose package contains it.\n")
        for text, labels in groups.items():
            o.write("\n---\n\n**Used by:** %s\n\n```\n%s\n```\n" % (", ".join(labels), text.replace("```", "'''")))
    print("%d crates, %d distinct texts -> %s" % (len(crates), len(groups), a.out), file=sys.stderr)


main()
