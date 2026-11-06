#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The one-click Claude Desktop bundle: an MCP Bundle (.mcpb, https://github.com/modelcontextprotocol/mcpb, manifest
0.3) holding this OS's sculptforge-cli, which Claude Desktop starts as `sculptforge-cli --mcp` (stdio, nothing on the
network). One bundle per OS, built by packages.yml next to the other packages and attached to every release;
mcp_server_json.py describes them for the MCP registry.

    build_mcpb.py --cli build/sculptforge-cli --version 0.11.3 --platform linux --out build/SculptForge-0.11.3-linux-x86_64.mcpb
    build_mcpb.py --cli ... --version auto --out "build/SculptForge-{version}-darwin-universal.mcpb"   (CMakeLists.txt's)
    build_mcpb.py --self-test

A .mcpb is a zip with manifest.json at its root (the format `mcpb pack` writes); this writes it directly so packaging
needs no Node. On Windows the DLLs beside the CLI come along. After packing, --check (default on) unpacks the bundle
and runs its server through tests/release_smoke.py's MCP check, the way Claude Desktop would start it.
"""
import argparse
import json
import os
import re
import stat
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ICON = ROOT / "assets" / "branding" / "sculptforge-256.png"
PLATFORMS = ("win32", "darwin", "linux")


def manifest(version, platform):
    exe = "server/sculptforge-cli" + (".exe" if platform == "win32" else "")
    return {
        "manifest_version": "0.3",
        "name": "sculptforge",
        "display_name": "SculptForge",
        "version": version,
        "description": "Digital sculpting for AI agents: make, sculpt, remesh, paint and export 3D models on your computer.",
        "long_description": "SculptForge's tools for Claude: start from a sphere, a model file or a picture; sculpt, "
                            "remesh, retopologise, unwrap, bake and export (glTF, USD, FBX, OBJ, LODs for game engines). "
                            "Runs on your computer; nothing listens on the network. Free and open source (GPL-3.0).",
        "author": {"name": "The SculptForge contributors", "url": "https://sculptforge.app"},
        "homepage": "https://sculptforge.app",
        "documentation": "https://sculptforge.app/guide/scripting.html",
        "support": "https://github.com/islandmed/SculptForge/issues",
        "repository": {"type": "git", "url": "https://github.com/islandmed/SculptForge"},
        "license": "GPL-3.0-or-later",
        "icon": "icon.png",
        "keywords": ["3d", "sculpting", "modeling", "mesh", "gltf", "usd", "game assets"],
        "server": {
            "type": "binary",
            "entry_point": exe,
            "mcp_config": {"command": "${__dirname}/" + exe, "args": ["--mcp"], "env": {}},
        },
        "tools_generated": True,  # every SculptForge command is a tool (sculptforge-cli --schema lists them)
        "compatibility": {"platforms": [platform]},
    }


def check_manifest(m):
    problems = [f"missing {k}" for k in ("manifest_version", "name", "version", "description", "author", "server") if k not in m]
    if not m.get("author", {}).get("name"):
        problems.append("author.name is required")
    if m.get("server", {}).get("type") != "binary" or "${__dirname}" not in m["server"]["mcp_config"]["command"]:
        problems.append("server must be a binary started from ${__dirname}")
    if not all(p in PLATFORMS for p in m.get("compatibility", {}).get("platforms", [])):
        problems.append("unknown platform")
    return problems


def pack(cli, version, platform, out):
    cli = Path(cli)
    m = manifest(version, platform)
    problems = check_manifest(m)
    if problems:
        sys.exit("build_mcpb: " + "; ".join(problems))
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(m, indent=2) + "\n")
        z.write(ICON, "icon.png")
        entries = [(cli, m["server"]["entry_point"])]
        if platform == "win32":  # the runtime DLLs the CLI was built with sit beside it
            entries += [(d, "server/" + d.name) for d in sorted(cli.parent.glob("*.dll"))]
        for src, name in entries:
            info = zipfile.ZipInfo.from_file(src, name)
            info.external_attr = (stat.S_IFREG | 0o755) << 16  # executable when unpacked (macOS, Linux)
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(src, "rb") as f:
                z.writestr(info, f.read())
    return out


def check_bundle(bundle):
    """Unpack the bundle and talk to its server as Claude Desktop would."""
    sys.path.insert(0, str(ROOT / "tests"))
    import release_smoke  # noqa: E402 (the MCP check lives with the other release smoke tests)
    with tempfile.TemporaryDirectory(prefix="sf-mcpb-") as tmp:
        with zipfile.ZipFile(bundle) as z:
            z.extractall(tmp)
            m = json.loads(z.read("manifest.json"))
        exe = Path(tmp) / m["server"]["entry_point"]
        exe.chmod(0o755)
        failures = release_smoke.mcp_check([str(exe)])
    for f in failures:
        print("FAILED:", f)
    return not failures


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        fake = Path(tmp) / "sculptforge-cli.exe"
        fake.write_bytes(b"MZ")
        (Path(tmp) / "runtime.dll").write_bytes(b"dll")
        out = pack(fake, "1.2.3", "win32", Path(tmp) / "x.mcpb")
        with zipfile.ZipFile(out) as z:
            names = set(z.namelist())
            m = json.loads(z.read("manifest.json"))
        assert names == {"manifest.json", "icon.png", "server/sculptforge-cli.exe", "server/runtime.dll"}, names
        assert m["version"] == "1.2.3" and m["server"]["mcp_config"]["command"] == "${__dirname}/server/sculptforge-cli.exe"
        assert not check_manifest(m) and check_manifest({"name": "x"})
        assert not check_manifest(manifest("1.0.0", "darwin")) and manifest("1.0.0", "linux")["server"]["entry_point"] == "server/sculptforge-cli"
    print("build_mcpb self-test: OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--cli")
    ap.add_argument("--version")
    ap.add_argument("--platform", choices=PLATFORMS)
    ap.add_argument("--out")
    ap.add_argument("--no-check", action="store_true", help="skip running the bundled server")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        self_test()
        return 0
    if not (a.cli and a.version and a.platform and a.out):
        ap.error("--cli, --version, --platform and --out are required")
    if a.version == "auto":  # the project's version (CMakeLists.txt), as CPack names the packages
        a.version = re.search(r"project\(SculptForge VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    out = pack(a.cli, a.version, a.platform, a.out.replace("{version}", a.version))
    print(f"{out} ({os.path.getsize(out) // 1024} KB)")
    if not a.no_check and not check_bundle(out):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
