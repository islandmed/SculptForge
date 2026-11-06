#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Release smoke test for a packaged sculptforge-cli (.github/workflows/release.yml runs it on every installed or
# unpacked package before anything is published; ctest runs it on the build tree): open a model, sculpt one stroke,
# save a project, open that project again and check that it holds the sculpted mesh. Then the MCP server (--mcp): the
# handshake, the tools and recipes, a tool call and a viewport_image picture, as an AI client would.
#
#   python3 tests/release_smoke.py --cli path/to/sculptforge-cli [--out DIR]
#   python3 tests/release_smoke.py --mcp-only --cli "SculptForge.AppImage --cli"   only the MCP check, through any
#                                       command that ends up in sculptforge-cli (the AppImage, a .mcpb's server)
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = os.path.join(HERE, "data", "marble-bust.glb")  # 17456 triangles, about 0.27 x 0.52 x 0.30


def run(cli, script):
    p = subprocess.run([cli, "-c", script], capture_output=True, text=True, timeout=300)
    sys.stdout.write(p.stdout)
    sys.stderr.write(p.stderr)
    if p.returncode != 0:
        sys.exit(f"sculptforge-cli failed ({p.returncode}): {script}")
    return p.stdout


def vertices(path):
    with open(path, encoding="utf-8") as f:
        return [line for line in f if line.startswith("v ")]


def mcp_check(command):
    """The MCP server as a client sees it: returns a list of failures."""
    lines = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize",
         "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "smoke", "version": "1"}}},
        {"jsonrpc": "2.0", "method": "notifications/initialized"},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
        {"jsonrpc": "2.0", "id": 3, "method": "prompts/list"},
        {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "new_sphere", "arguments": {"subdivisions": 3}}},
        {"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "viewport_image", "arguments": {"size": 256}}},
    ]
    p = subprocess.run(command + ["--mcp"], input="".join(json.dumps(m) + "\n" for m in lines),
                       capture_output=True, text=True, timeout=120)
    if p.returncode != 0:
        return [f"--mcp exited with {p.returncode}: {p.stderr.strip()[-500:]}"]
    replies = {}
    for line in p.stdout.splitlines():  # stdout is the protocol: every line must be JSON-RPC
        try:
            r = json.loads(line)
        except ValueError:
            return [f"--mcp wrote a line that isn't JSON: {line[:200]}"]
        replies[r.get("id")] = r
    failures = []
    init = replies.get(1, {}).get("result", {})
    if init.get("serverInfo", {}).get("name") != "sculptforge" or "prompts" not in init.get("capabilities", {}):
        failures.append(f"initialize: {replies.get(1)}")
    tools = {t["name"] for t in replies.get(2, {}).get("result", {}).get("tools", [])}
    if not {"remesh", "export_glb", "viewport_image"} <= tools:
        failures.append(f"tools/list lacks remesh, export_glb or viewport_image ({len(tools)} tools)")
    if len(replies.get(3, {}).get("result", {}).get("prompts", [])) < 5:
        failures.append(f"prompts/list: {replies.get(3)}")
    if replies.get(4, {}).get("result", {}).get("isError", True):
        failures.append(f"new_sphere: {replies.get(4)}")
    image = (replies.get(5, {}).get("result", {}).get("content") or [{}])[0]
    if image.get("type") != "image" or len(image.get("data", "")) < 1000:
        failures.append(f"viewport_image gave no picture: {str(replies.get(5))[:300]}")
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--out")
    ap.add_argument("--mcp-only", action="store_true")
    a = ap.parse_args()
    if a.mcp_only:
        failures = mcp_check(shlex.split(a.cli, posix=os.name != "nt"))
        for f in failures:
            print("FAILED:", f)
        print("MCP smoke: " + ("FAILED" if failures else "OK"))
        return 1 if failures else 0
    out = a.out or tempfile.mkdtemp(prefix="sf-release-smoke-")
    os.makedirs(out, exist_ok=True)
    before, after, again = (os.path.join(out, n) for n in ("before.obj", "after.obj", "reopened.obj"))
    project = os.path.join(out, "smoke.sfproj")

    # A Clay stroke across the face, drawn in front of it and projected onto the surface (front view).
    run(a.cli, f'open path="{MODEL}"; save_obj path="{before}"; '
               f'stroke brush=Clay points="-0.06,0.30,0.3 0.0,0.33,0.3 0.06,0.30,0.3" strength=1; '
               f'save_project path="{project}"; save_obj path="{after}"')
    info = run(a.cli, f'open path="{project}"; mesh_info; save_obj path="{again}"')

    failures = []
    if not os.path.getsize(project) > 0:
        failures.append("the saved project is empty")
    m = re.search(r"(\d+) triangles", info)
    if not m or int(m.group(1)) != 17456:
        failures.append(f"the reopened project should have 17456 triangles: {info.strip()}")
    vb, va, vr = vertices(before), vertices(after), vertices(again)
    if len(vb) != len(va) or not vb:
        failures.append(f"vertex count changed by a sculpt stroke: {len(vb)} -> {len(va)}")
    moved = sum(1 for x, y in zip(vb, va) if x != y)
    if moved == 0:
        failures.append("the stroke moved no vertex")
    if va != vr:
        failures.append("the reopened project's mesh differs from the one saved")
    failures += mcp_check([a.cli])
    for f in failures:
        print("FAILED:", f)
    if failures:
        return 1
    print(f"release smoke: OK ({moved} vertices moved by the stroke, project {os.path.getsize(project)} bytes; MCP server OK)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
