#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes a .sfplugin: a WebAssembly module with its manifest in a "sculptforge.plugin" custom section.

    sfplugin.py module.wasm manifest.json -o "My Plugin.sfplugin"

Checks what SculptForge checks before it installs one (src/core/Plugin.cpp): a WebAssembly 1.0 module, no imports,
exports memory, sf_alloc and sf_run, and a manifest that is strict JSON with the right format. An existing
manifest section is replaced. Put the result in a pack's plugins/ folder (docs/pack-format.md).
"""
import argparse
import json
import sys

SECTION = "sculptforge.plugin"


def leb(data, at):
    value = shift = 0
    while True:
        if at >= len(data) or shift > 28:
            raise ValueError("damaged LEB128 number")
        b = data[at]
        at += 1
        value |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return value, at


def enc(value):
    out = bytearray()
    while True:
        b = value & 0x7F
        value >>= 7
        out.append(b | 0x80 if value else b)
        if not value:
            return bytes(out)


def name(data, at):
    n, at = leb(data, at)
    return data[at:at + n].decode("utf-8"), at + n


def sections(data):
    if data[:8] != b"\0asm\1\0\0\0":
        raise ValueError("not a WebAssembly 1.0 core module")
    at = 8
    while at < len(data):
        sid = data[at]
        size, body = leb(data, at + 1)
        if body + size > len(data):
            raise ValueError("a section runs past the end")
        yield sid, at, body, body + size
        at = body + size


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("module")
    ap.add_argument("manifest")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()
    wasm = open(a.module, "rb").read()
    text = open(a.manifest, encoding="utf-8").read()

    def strict(pairs):
        keys = [k for k, _ in pairs]
        if len(keys) != len(set(keys)):
            raise ValueError("a key given twice")
        return dict(pairs)

    m = json.loads(text, object_pairs_hook=strict)
    if m.get("format") != "sculptforge-plugin" or m.get("formatVersion") != 1:
        sys.exit('manifest: "format" must be "sculptforge-plugin" and "formatVersion" 1')
    if m.get("kind") not in ("deformer", "generator"):
        sys.exit('manifest: "kind" must be "deformer" or "generator"')
    if m.get("permissions"):
        sys.exit("manifest: phase 1 plugins get no permissions; leave \"permissions\" empty")

    kept = bytearray(wasm[:8])
    exports = {}
    for sid, start, body, end in sections(wasm):
        if sid == 0 and name(wasm, body)[0] == SECTION:
            continue  # replaced below
        if sid == 2 and leb(wasm, body)[0]:
            sys.exit("the module imports something; plugins can't import anything (build without WASI or libc)")
        if sid == 7:
            count, at = leb(wasm, body)
            for _ in range(count):
                n, at = name(wasm, at)
                kind = wasm[at]
                _, at = leb(wasm, at + 1)
                exports[n] = kind
        kept += wasm[start:end]
    for n, kind in (("memory", 2), ("sf_alloc", 0), ("sf_run", 0)):
        if exports.get(n) != kind:
            sys.exit(f"the module must export {n}")
    payload = enc(len(SECTION)) + SECTION.encode() + json.dumps(m, ensure_ascii=False, indent=1).encode("utf-8")
    kept += b"\0" + enc(len(payload)) + payload
    if len(kept) > 16 << 20:
        sys.exit("a plugin may be at most 16 MB")
    open(a.output, "wb").write(kept)
    print(f"wrote {a.output} ({len(kept)} bytes): {m['name']} {m['version']} ({m['kind']})")


if __name__ == "__main__":
    main()
