#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes the release's software bill of materials (CycloneDX 1.5 JSON) for IT reviews (docs/guide/security.md).

    tools/release/sbom.py --version 0.11.4 --out dist/SculptForge-0.11.4-sbom.cdx.json
    tools/release/sbom.py --check        every fetched pin is in THIRD_PARTY_NOTICES.md (ctest sbom)

Components come from THIRD_PARTY_NOTICES.md's first table (name, version, licence, what for); the archives the build
fetches add their URL and SHA-256 from cmake/Fetch.cmake's pins. The release workflow writes it into dist/ before the
checksums, so SHA256SUMS.txt and its signature cover it.
"""
import argparse
import json
import re
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NOTICES = ROOT / "THIRD_PARTY_NOTICES.md"
FETCH = ROOT / "cmake" / "Fetch.cmake"

# Fetch.cmake pin name -> the component's name in THIRD_PARTY_NOTICES.md
PIN_NAMES = {"glm": "GLM", "SDL3": "SDL 3", "imgui": "Dear ImGui", "mbedtls": "Mbed TLS", "libdatachannel": "libdatachannel",
             "ldc_usrsctp": "usrsctp", "ldc_libjuice": "libjuice", "ldc_plog": "plog", "pybind11": "pybind11",
             **{f"wasmtime_{p}": "Wasmtime" for p in ("linux_x86_64", "linux_aarch64", "macos_x86_64", "macos_aarch64",
                                                      "windows_x86_64")}}
LICENSES = {"MIT License": "MIT", "zlib License": "Zlib", "BSD 3-Clause License": "BSD-3-Clause",
            "Apache License 2.0": "Apache-2.0", "Mozilla Public License 2.0": "MPL-2.0",
            "SIL Open Font License 1.1": "OFL-1.1", "CC0 1.0 (public domain)": "CC0-1.0", "BSD-style": "BSD-3-Clause"}


def pins():
    text = FETCH.read_text(encoding="utf-8")
    return {m[0]: (m[1], m[2]) for m in re.findall(r"set\(SF_PIN_(\w+)\s+(\S+)\s+([0-9a-f]{64})\)", text)}


def notice_rows():
    rows = []
    for line in NOTICES.read_text(encoding="utf-8").splitlines():
        if line.startswith("| Tool |"):
            break  # the second table: tools downloaded at run time, not in the binaries
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) < 4 or cells[0] in ("Component", "") or set(cells[0]) <= {"-", " "}:
            continue
        m = re.match(r"\[([^\]]+)\]\(([^)]+)\)", cells[0])
        name, url = (m.group(1), m.group(2)) if m else (cells[0], "")
        rows.append({"name": name, "url": url, "version": cells[1], "license": cells[2], "use": cells[3]})
    return rows


def license_entry(text):
    if text.startswith("Apache License 2.0 with LLVM Exception"):
        return {"expression": "Apache-2.0 WITH LLVM-exception"}
    for k, v in LICENSES.items():
        if text.startswith(k):
            return {"license": {"id": v}}
    return {"license": {"name": text[:120]}}


def build(version):
    p = pins()
    by_name = {v: k for k, v in PIN_NAMES.items()}
    components = []
    for r in notice_rows():
        if "not shipped" in r["use"] or "nothing vendored" in r["version"] or r["version"] in ("—", "colour values only") or "formulas only" in r["version"]:
            continue  # credited references, not code in the binaries
        c = {"type": "library", "name": r["name"], "version": r["version"], "description": r["use"][:300],
             "licenses": [license_entry(r["license"])]}
        if r["url"]:
            c["externalReferences"] = [{"type": "website", "url": r["url"]}]
        if r["name"] == "Wasmtime":  # one prebuilt archive per platform (cmake/Wasmtime.cmake)
            for k in sorted(k for k in p if k.startswith("wasmtime_")):
                c.setdefault("hashes", []).append({"alg": "SHA-256", "content": p[k][1]})
                c.setdefault("externalReferences", []).append({"type": "distribution", "url": p[k][0]})
        pin = None if r["name"] == "Wasmtime" else by_name.get(r["name"])
        if pin and pin in p:
            url, sha = p[pin]
            c["hashes"] = [{"alg": "SHA-256", "content": sha}]
            c.setdefault("externalReferences", []).append({"type": "distribution", "url": url})
        components.append(c)
    return {
        "bomFormat": "CycloneDX", "specVersion": "1.5", "version": 1,
        "serialNumber": "urn:uuid:" + str(uuid.uuid5(uuid.NAMESPACE_URL, "https://sculptforge.app/sbom/" + version)),
        "metadata": {"component": {"type": "application", "name": "SculptForge", "version": version,
                                   "licenses": [{"license": {"id": "GPL-3.0-or-later"}}]}},
        "components": components,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="0.0.0")
    ap.add_argument("--out")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if a.check:
        names = {r["name"] for r in notice_rows()}
        missing = [PIN_NAMES.get(k, k) for k in pins() if PIN_NAMES.get(k, k) not in names and k != "pybind11"]
        unmapped = [k for k in pins() if k not in PIN_NAMES]
        if missing or unmapped:
            print("sbom: fetched but not in THIRD_PARTY_NOTICES.md:", ", ".join(missing + unmapped))
            return 1
        print(f"sbom: {len(build(a.version)['components'])} components")
        return 0
    text = json.dumps(build(a.version), indent=2) + "\n"
    if a.out:
        Path(a.out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
