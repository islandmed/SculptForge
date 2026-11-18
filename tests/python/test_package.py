# SculptForge - the package itself: type stubs cover the module, nothing GUI is loaded, the example scripts run.
# SPDX-License-Identifier: GPL-3.0-or-later
import ast
import os
import pathlib
import runpy
import sys

import pytest

import sculptforge as sf
from sculptforge import _core


def stub_names():
    stub = pathlib.Path(_core.__file__).with_name("_core.pyi")
    assert stub.exists(), "the type stubs ship beside the module"
    assert stub.with_name("py.typed").exists()
    names = set()
    for node in ast.parse(stub.read_text()).body:
        if isinstance(node, (ast.FunctionDef, ast.ClassDef)):
            names.add(node.name)
            if node.name == "Document":
                names |= {f"Document.{n.name}" for n in node.body if isinstance(n, ast.FunctionDef)}
        elif isinstance(node, ast.AnnAssign):
            names.add(node.target.id)
    return names


def test_stubs_cover_the_module():
    names = stub_names()
    public = {n for n in dir(_core) if not n.startswith("_")} | {"__version__"}
    assert public <= names, public - names
    members = {f"Document.{n}" for n in dir(sf.Document) if not n.startswith("_")}
    assert members <= names, members - names


@pytest.mark.skipif(not sys.platform.startswith("linux"), reason="reads /proc/self/maps")
def test_engine_only():
    # No window system, GPU or HTTP library in the process (the wheels: also no libcurl, SCULPTFORGE_HTTP=OFF).
    maps = pathlib.Path("/proc/self/maps").read_text()
    banned = ["libSDL", "libGL", "libEGL", "libX11", "libwayland"] + (["libcurl"] if os.environ.get("SF_WHEEL_TEST") else [])
    for lib in banned:
        assert lib not in maps, lib


@pytest.mark.skipif(not os.environ.get("SF_WHEEL_TEST"), reason="only the wheels are built without HTTP")
def test_wheel_has_no_http():
    # The engine's own HTTP client says it has none (src/ai/Http.cpp, SF_NO_HTTP), so no command can reach a server.
    binary = pathlib.Path(_core.__file__).read_bytes()
    assert b"built with SCULPTFORGE_HTTP=OFF" in binary
    assert b"winhttp" not in binary.lower() and b"libcurl.so" not in binary


EXAMPLES = pathlib.Path(__file__).resolve().parents[2] / "examples" / "python"


@pytest.mark.parametrize("name", ["batch_export.py", "procedural_asset.py"])
def test_examples_run(tmp_path, monkeypatch, name):
    if not (EXAMPLES / name).exists():
        pytest.skip("examples not in this checkout")
    monkeypatch.chdir(tmp_path)
    monkeypatch.setattr(sys, "argv", [name, str(tmp_path / "out")])
    runpy.run_path(str(EXAMPLES / name), run_name="__main__")
    assert any((tmp_path / "out").iterdir())
