#!/usr/bin/env python3
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that every shader the renderer compiles is valid GLSL ES 3.20 (the Android renderer) and links with its partners.

The renderer writes its shaders once, as "#version 330 core", and src/render/GL.cpp (shaderSourceES) swaps in the ES
header when it compiles them on Android. Desktop Mesa accepts things a phone's driver doesn't, so this takes the same
sources, puts on the same header and runs glslangValidator on every program (stage by stage, then linked):

    tools/dev/check_gles_shaders.py            exit 1 on an error (ctest gles_shaders; skipped when glslangValidator is missing)
    tools/dev/check_gles_shaders.py --dump DIR write every ES source and stop (to look at one, or to try another validator)

A shader added to src/render needs a line in PROGRAMS below (this check fails on one it doesn't know), so it is checked too.
"""
import itertools
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# GL.cpp shaderSourceES: keep in step (the check below fails when the header there changes).
ES_HEADER = ("#version 320 es\n"
             "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
             "precision highp usampler2D;\nprecision highp isampler2D;\nprecision highp samplerBuffer;\n"
             "precision highp usamplerBuffer;\nprecision highp isamplerBuffer;\n")

# Renderer::meshProgram's feature bits, as #defines
MESH_FEATURES = ["PAINT", "PRIM", "TEXTURED", "NORMALMAP", "FLAT", "GHOST", "PBR"]
# the combinations the renderer asks for (per-triangle data comes with TEXTURED and NORMALMAP: meshProgram adds PRIM)
MESH_SETS = [[], ["PAINT"], ["PRIM"], ["FLAT"], ["GHOST"], ["PBR"], ["PAINT", "PBR"], ["TEXTURED"], ["TEXTURED", "PAINT"],
             ["TEXTURED", "FLAT"], ["TEXTURED", "PBR"], ["TEXTURED", "NORMALMAP"], ["TEXTURED", "NORMALMAP", "PAINT", "PBR"],
             ["TEXTURED", "NORMALMAP", "PAINT", "PBR", "FLAT"], ["TEXTURED", "PAINT", "FLAT"]]
MICRO_SETS = [["MICRO"], ["MICRO", "FOCUS"], ["MICRO", "TILES"], ["MICRO", "PBR"], ["MICRO", "FLAT"]]
# (WIDE: a base mesh past a small GPU's buffer-texture limit, the per-slot tables are 2D textures: render/TexelStore.h)
MICRO_SETS += [[*s, "WIDE"] for s in MICRO_SETS]

# every program: name -> (file, [(stage, constant or constant + constant)...], defines)
# stage is vert, frag or geom; "A+B" is the text of A followed by B.
PROGRAMS = {
    "wire": ("Renderer.cpp", [("vert", "kWireVS"), ("frag", "kWireFS")]),
    "wire lines": ("Renderer.cpp", [("vert", "kWireVS"), ("geom", "kWireLineGS"), ("frag", "kWireLineFS")]),
    "vertex id probe": ("Renderer.cpp", [("vert", "kVertexIdVS"), ("frag", "kVertexIdFS")]),
    "background": ("Renderer.cpp", [("vert", "kBgVS"), ("frag", "kBgFS")]),
    "reference image": ("Renderer.cpp", [("vert", "kReferenceVS"), ("frag", "kReferenceFS")]),
    "floor": ("Renderer.cpp", [("vert", "kFloorVS"), ("frag", "kFloorFS")]),
    "environment background": ("Renderer.cpp", [("vert", "kBgVS"), ("frag", "kEnvBgFS")]),
    "splats": ("Renderer.cpp", [("vert", "kSplatSH+kSplatVS"), ("frag", "kSplatFS")]),
    "splat colour": ("Renderer.cpp", [("vert", "kBgVS"), ("frag", "kSplatSH+kSplatColorFS")]),
    "shadows and AO": ("Occlusion.cpp", [("vert", "kVS"), ("frag", "kOccFS")]),
    "shadows blur": ("Occlusion.cpp", [("vert", "kVS"), ("frag", "kBlurFS")]),
    "shadows composite": ("Occlusion.cpp", [("vert", "kVS"), ("frag", "kCompFS")]),
    "UV view background": ("UvView.cpp", [("vert", "kBgVS"), ("frag", "kBgFS")]),
    "UV view mesh": ("UvView.cpp", [("vert", "kMeshVS"), ("frag", "kMeshFS")]),
    "screen stroke": ("ScreenStroke.cpp", [("vert", "kVS"), ("frag", "kFS")]),
}
# shaders outside src/render that are desktop-only today (the Android build leaves them out), so not checked
SKIPPED = {"src/app/Calibration.cpp", "src/app/OptimisticBench.cpp"}

TOKEN = re.compile(r'R"([^()\\\s]{0,16})\((.*?)\)\1"|"((?:[^"\\\n]|\\.)*)"|(//[^\n]*)|([A-Za-z_]\w*)', re.S)


def unescape(s):
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), s)


def evaluate(expr, macros):
    """The text of a C++ expression made of string literals, raw string literals and macros that are those."""
    out = []
    for m in TOKEN.finditer(expr):
        if m.group(2) is not None:
            out.append(m.group(2))
        elif m.group(3) is not None:
            out.append(unescape(m.group(3)))
        elif m.group(5) in macros: # (group 4 is a comment between two literals)
            out.append(macros[m.group(5)])
    return "".join(out)


def constants(path):
    """name -> text of every `const char* kName = <string literals>;` in a source file."""
    text = path.read_text(encoding="utf-8")
    macros = {}
    for m in re.finditer(r"^#define\s+(\w+)((?:[^\n]*\\\n)*[^\n]*)", text, re.M):
        macros[m.group(1)] = evaluate(m.group(2).replace("\\\n", "\n"), {})
    found = {}
    for m in re.finditer(r"\bconst char\*\s+(k\w+)\s*=\s*", text):
        i, depth, raw = m.end(), 0, None
        # the end of the declaration: the first ';' outside a literal
        while i < len(text):
            if text.startswith('R"', i):
                d = re.match(r'R"([^()\\\s]{0,16})\(', text[i:])
                i = text.index(")" + d.group(1) + '"', i) + len(d.group(1)) + 2
                continue
            if text[i] == '"':
                i += 1
                while text[i] != '"':
                    i += 2 if text[i] == "\\" else 1
            elif text.startswith("//", i):
                i = text.index("\n", i)
            elif text[i] == ";":
                break
            i += 1
        found[m.group(1)] = evaluate(text[m.end():i], macros)
    return found


def validator():
    return shutil.which("glslangValidator") or shutil.which("glslang")


def es_source(body, defines):
    """What shaderSourceES hands the driver: the ES header in place of the desktop #version line, after the defines."""
    prefix = "".join(f"#define {d}\n" for d in defines)
    src = prefix + body if not body.lstrip().startswith("#version") else None
    if src is None:  # a shader with its own "#version 330 core" line: the header replaces it, defines go after
        rest = body.split("\n", 1)[1] if "\n" in body else ""
        src = prefix + rest
    return ES_HEADER + src


def main():
    dump = Path(sys.argv[2]) if len(sys.argv) == 3 and sys.argv[1] == "--dump" else None
    glsl = validator()
    if not glsl and not dump:
        print("glslangValidator not found: GLES shaders not checked")
        return 77  # (ctest SKIP_RETURN_CODE)
    gl_cpp = (ROOT / "src/render/GL.cpp").read_text(encoding="utf-8")
    if "#version 320 es" not in gl_cpp or ES_HEADER.splitlines()[1] not in gl_cpp.replace('\\n"\n', "\n").replace('"', ""):
        print("src/render/GL.cpp: the GLES shader header changed; update ES_HEADER in tools/dev/check_gles_shaders.py")
        return 1
    files = {p.name: constants(p) for p in (ROOT / "src/render").glob("*.cpp")}
    errors = []
    # a shader constant this check doesn't know
    known = {(f, c) for f, parts, *_ in PROGRAMS.values() for _, expr in parts for c in expr.split("+")}
    known |= {("Renderer.cpp", c) for c in ("kMeshVS", "kMeshFS", "kMeshGS")} | {("MicroRenderer.cpp", "kMicroVS")}
    for fname, consts in files.items():
        for name, text in consts.items():
            if ("#version 330" in text or name.endswith(("VS", "FS", "GS"))) and (fname, name) not in known:
                if not any(fname in s for s in SKIPPED):
                    errors.append(f"src/render/{fname}: shader {name} is not listed in tools/dev/check_gles_shaders.py PROGRAMS")
    jobs = []  # (name, [(stage, source)])
    for name, (fname, parts) in PROGRAMS.items():
        stages = []
        for stage, expr in parts:
            try:
                body = "".join(files[fname][c] for c in expr.split("+"))
            except KeyError as e:
                errors.append(f"{name}: no constant {e} in src/render/{fname}")
                break
            stages.append((stage, es_source(body, [])))
        else:
            jobs.append((name, stages))
    renderer = files["Renderer.cpp"]
    micro = files["MicroRenderer.cpp"]["kMicroVS"]
    for feats in MESH_SETS:
        defs = list(dict.fromkeys(feats + (["PRIM"] if {"TEXTURED", "NORMALMAP"} & set(feats) else [])))
        stages = [("vert", es_source(renderer["kMeshVS"], defs)), ("frag", es_source(renderer["kMeshFS"], defs))]
        if "TEXTURED" in defs:
            stages.insert(1, ("geom", es_source(renderer["kMeshGS"], defs)))
        jobs.append(("mesh " + "+".join(feats or ["plain"]), stages))
    for feats in MICRO_SETS:
        # (micro programs: the mesh fragment shader under MICRO, no per-vertex paint, texture or per-triangle data)
        stages = [("vert", es_source(micro, feats)), ("frag", es_source(renderer["kMeshFS"], feats))]
        jobs.append(("micro " + "+".join(feats), stages))
    if errors:
        print("\n".join(errors))
    work = dump or Path(tempfile.mkdtemp(prefix="sf-gles-"))
    work.mkdir(parents=True, exist_ok=True)
    try:
        for n, (name, stages) in enumerate(jobs):
            paths = []
            for stage, src in stages:
                p = work / f"{n:02d}-{re.sub(r'[^A-Za-z0-9+]+', '_', name)}.{stage}"
                p.write_text(src, encoding="utf-8")
                paths.append(p)
            if dump:
                continue
            for p in paths:  # each stage on its own: the error names the stage and line
                r = subprocess.run([glsl, "-S", p.suffix[1:], str(p)], capture_output=True, text=True)
                if r.returncode:
                    errors.append(f"{name}: {p.suffix[1:]} shader is not valid GLSL ES 3.20:\n" + "\n".join(
                        l for l in (r.stdout + r.stderr).splitlines() if l.startswith(("ERROR", "WARNING: 0")) or "error" in l.lower()))
            r = subprocess.run([glsl, "-l", *map(str, paths)], capture_output=True, text=True)  # linked: the stages' interfaces
            if r.returncode:
                errors.append(f"{name}: stages do not link:\n" + "\n".join(l for l in (r.stdout + r.stderr).splitlines() if "ERROR" in l))
    finally:
        if not dump:
            shutil.rmtree(work, ignore_errors=True)
    if dump:
        print(f"{len(jobs)} programs written to {work}")
        return 0
    for e in errors:
        print(e)
    print(f"{len(jobs)} GLES programs checked, {len(errors)} problems")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
