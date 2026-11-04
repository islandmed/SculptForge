# SculptForge - a stand-in for sculptforge-cli used by the unit tests.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Understands the subset of commands the nodes use, keeps a toy "mesh" (triangle count) in
# JSON files, and appends every invocation to $FAKE_SF_LOG so tests can check the scripts.
# Standard library only, so it runs with any Python.
import json
import os
import struct
import sys


def fail(line, cmd, message):
    sys.stderr.write(f"error (line {line}, {cmd}): {message}\n")
    sys.exit(2)


def write_state(path, state, magic=b""):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(magic + json.dumps(state).encode())


def read_state(path):
    with open(path, "rb") as f:
        data = f.read()
    if data.startswith(b"ply"):
        header = data[:data.index(b"end_header")].decode()
        counts = {l.split()[1]: int(l.split()[2]) for l in header.splitlines() if l.startswith("element")}
        return {"name": os.path.splitext(os.path.basename(path))[0], "triangles": counts.get("face", 0)}
    if data.startswith(b"glTF"):
        data = data[12:]
    try:
        return json.loads(data)
    except ValueError:
        return {"name": os.path.splitext(os.path.basename(path))[0], "triangles": 100}


def write_ply(path):
    v = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (0, 0, 1)]
    f = [(0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)]
    header = ("ply\nformat binary_little_endian 1.0\nelement vertex 4\nproperty float x\nproperty float y\n"
              "property float z\nproperty float nx\nproperty float ny\nproperty float nz\nproperty uchar red\n"
              "property uchar green\nproperty uchar blue\nelement face 4\n"
              "property list uchar int vertex_indices\nend_header\n")
    with open(path, "wb") as out:
        out.write(header.encode())
        for p in v:
            out.write(struct.pack("<ffffffBBB", *p, 0, 0, 1, 255, 128, 0))
        for t in f:
            out.write(struct.pack("<Biii", 3, *t))


def main(argv):
    variables, script = {}, None
    i = 0
    while i < len(argv):
        if argv[i] == "--set":
            name, _, value = argv[i + 1].partition("=")
            variables[name] = value
            i += 2
        elif argv[i] == "-c":
            script = argv[i + 1]
            i += 2
        else:
            i += 1
    log = os.environ.get("FAKE_SF_LOG")
    if log:
        with open(log, "a", encoding="utf-8") as f:
            f.write(json.dumps({"vars": variables, "script": script}) + "\n")
    if os.environ.get("FAKE_SF_FAIL"):
        fail(1, "remesh", os.environ["FAKE_SF_FAIL"])

    state = None
    for line, statement in enumerate((s.strip() for s in script.split(";")), 1):
        if not statement:
            continue
        cmd, *tokens = statement.split()
        args = {}
        for tok in tokens:
            key, _, value = tok.partition("=")
            args[key] = variables[value[1:]] if value.startswith("$") else value
        path = args.get("path")
        if cmd == "open":
            if not os.path.isfile(path):
                fail(line, cmd, f"Cannot open {path}")
            state = read_state(path)
            print(f"open: Opened {path} ({state['triangles']} triangles)")
        elif cmd in ("image_inflate", "image_relief"):
            if not os.path.isfile(path):
                fail(line, cmd, f"Cannot read image {path}")
            state = {"name": os.path.splitext(os.path.basename(path))[0], "triangles": 1000}
            print(f"{cmd}: Inflated sketch into 1000 triangles")
        elif cmd == "remesh":
            state["triangles"] = int(args.get("resolution", 200)) * 10
            print(f"remesh: Remeshed to {state['triangles']} triangles")
        elif cmd == "decimate":
            state["triangles"] = int(args["triangles"]) if "triangles" in args else \
                int(state["triangles"] * float(args.get("ratio", 0.5)))
            print(f"decimate: Decimated to {state['triangles']} triangles")
        elif cmd == "subdivide":
            state["triangles"] *= 4 ** int(args.get("levels", 1))
            print(f"subdivide: Subdivided to {state['triangles']} triangles")
        elif cmd in ("save_project", "export_fbx", "export_usd", "save_obj", "export_stl"):
            write_state(path, state)
            print(f"{cmd}: Saved {path}")
        elif cmd == "export_glb":
            write_state(path, state, b"glTF" + struct.pack("<II", 2, 0))
            print(f"{cmd}: Exported {path}")
        elif cmd == "export_ply":
            write_ply(path)
            print(f"{cmd}: Exported {path}")
        elif cmd == "export_lods":
            base, ext = os.path.splitext(path)
            if not os.path.isdir(os.path.dirname(base)):
                fail(line, cmd, f"Cannot write {base}_LOD0{ext}")
            for level in range(int(args.get("levels", 4))):
                write_state(f"{base}_LOD{level}{ext}", state, b"glTF" + struct.pack("<II", 2, 0))
            print(f"{cmd}: Exported {args.get('levels', 4)} LODs")
        elif cmd == "mesh_info":
            print(f"mesh_info: {state['triangles'] // 2} vertices, {state['triangles']} triangles, "
                  "bounds (-1, -1, -1) - (1, 1, 1)")
        elif cmd == "subtool_list":
            print(f"subtool_list: 1 Part:\n0. {state['name']} ({state['triangles']} triangles, active)")  # marks-pending
        else:
            fail(line, cmd, f"Unknown command '{cmd}'")


if __name__ == "__main__":
    main(sys.argv[1:])
