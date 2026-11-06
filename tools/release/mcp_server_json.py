#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""server.json for the official MCP registry (registry.modelcontextprotocol.io): SculptForge as one server whose
packages are the release's .mcpb bundles (build_mcpb.py), each by its GitHub release URL and SHA-256. release.yml
writes it into the release; publishing it is the maintainer's (an account step):

    mcp-publisher login github          # as islandmed: the name below is io.github.islandmed/...
    mcp-publisher publish server.json   # the file from the release (or: mcp_server_json.py --version X.Y.Z --dist DIR)

Schema and rules: https://github.com/modelcontextprotocol/registry/blob/main/docs/reference/server-json/
(description at most 100 characters; mcpb packages only from GitHub or GitLab releases, URL containing "mcp", with
fileSha256).

    mcp_server_json.py --version 0.11.3 --dist dist [--out dist/server.json]
    mcp_server_json.py --self-test
"""
import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path

SCHEMA = "https://static.modelcontextprotocol.io/schemas/2025-12-11/server.schema.json"
NAME = "io.github.islandmed/sculptforge"
REPO = "https://github.com/islandmed/SculptForge"
DESCRIPTION = "Digital sculpting for AI agents: make, sculpt, remesh, paint and export 3D models locally."


def server_json(version, bundles):
    packages = []
    for b in sorted(bundles, key=lambda p: p.name):
        packages.append({
            "registryType": "mcpb",
            "identifier": f"{REPO}/releases/download/v{version}/{b.name}",
            "fileSha256": hashlib.sha256(b.read_bytes()).hexdigest(),
            "transport": {"type": "stdio"},
        })
    return {
        "$schema": SCHEMA,
        "name": NAME,
        "title": "SculptForge",
        "description": DESCRIPTION,
        "repository": {"url": REPO, "source": "github"},
        "websiteUrl": "https://sculptforge.app",
        "version": version,
        "packages": packages,
    }


def check(doc):
    problems = []
    if len(doc["description"]) > 100:
        problems.append("description over 100 characters")
    if not doc["packages"]:
        problems.append("no .mcpb bundles")
    for p in doc["packages"]:
        if "mcp" not in p["identifier"] or not p["identifier"].startswith("https://github.com/"):
            problems.append(f"bad identifier {p['identifier']}")
    return problems


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        b = Path(tmp) / "SculptForge-1.2.3-darwin-universal.mcpb"
        b.write_bytes(b"zip")
        doc = server_json("1.2.3", [b])
        assert not check(doc), check(doc)
        assert doc["packages"][0]["identifier"].endswith("/v1.2.3/SculptForge-1.2.3-darwin-universal.mcpb")
        assert doc["packages"][0]["fileSha256"] == hashlib.sha256(b"zip").hexdigest()
        assert check(server_json("1.2.3", []))
    print("mcp_server_json self-test: OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version")
    ap.add_argument("--dist")
    ap.add_argument("--out")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        self_test()
        return 0
    if not (a.version and a.dist):
        ap.error("--version and --dist are required")
    # The versioned bundles only (release.yml also adds version-less copies for fixed links).
    bundles = [p for p in Path(a.dist).glob("SculptForge-*.mcpb") if f"-{a.version}-" in p.name]
    doc = server_json(a.version, bundles)
    problems = check(doc)
    if problems:
        sys.exit("mcp_server_json: " + "; ".join(problems))
    out = Path(a.out or Path(a.dist) / "server.json")
    out.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
    print(f"{out}: {len(bundles)} bundles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
