# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
"""The Pythonic layer over the compiled Document: open and save by file name, strokes from NumPy paths, the common
mesh operations as methods. Every method runs the same command `sf.run` would (the result is byte for byte the
same as the command line's), so `sf.commands()` documents the details."""
from __future__ import annotations

import os
from typing import Any, Union

import numpy as np
from numpy.typing import ArrayLike

from . import _core

PathLike = Union[str, "os.PathLike[str]"]

# Save and export by file name: the command for each suffix.
_WRITERS = {
    ".sfproj": "save_project",
    ".obj": "save_obj",
    ".glb": "export_glb",
    ".gltf": "export_glb",
    ".usda": "export_usd",
    ".usdz": "export_usd",
    ".usd": "export_usd",
    ".fbx": "export_fbx",
    ".ply": "export_ply",
    ".stl": "export_stl",
}


def _suffix(path: PathLike) -> str:
    return os.path.splitext(os.fspath(path))[1].lower()


def _number(x: Any) -> str:
    # 9 significant digits: every float32 comes back exactly, the same text a script would hold.
    return format(float(x), ".9g")


def _points(points: ArrayLike) -> str:
    a = np.asarray(points, dtype=np.float64)
    if a.ndim == 1 and a.shape == (3,):
        a = a[None, :]
    if a.ndim != 2 or a.shape[1] != 3 or len(a) == 0:
        raise ValueError(f"points: expected (K, 3) positions, got shape {a.shape}")
    if not np.isfinite(a).all():
        raise ValueError("points: NaN or infinity")
    return " ".join(",".join(_number(v) for v in p) for p in a)


def _vec3(v: ArrayLike, what: str) -> str:
    a = np.asarray(v, dtype=np.float64).reshape(-1)
    if a.shape != (3,) or not np.isfinite(a).all():
        raise ValueError(f"{what}: expected x, y, z")
    return ",".join(_number(x) for x in a)


def _args(**kwargs: Any) -> dict[str, Any]:
    """The arguments that were given (None = the command's default)."""
    return {k: v for k, v in kwargs.items() if v is not None}


class Document(_core.Document):
    """A scene: Parts (separate meshes, each with layers and subdivision levels), Gaussian splats and edit history.

    The mesh properties (vertices, mask, facegroups, ...) and the methods act on the active Part.
    """

    # --- files ---

    @classmethod
    def open(cls, path: PathLike, *, unit: str | None = None) -> "Document":
        """A new Document with the file in it: a project (.sfproj) or a model (OBJ, PLY, FBX, glTF/GLB, USD, STL).
        `unit` is the model's unit for formats that don't say (e.g. "cm")."""
        doc = cls()
        if _suffix(path) == ".sfproj":
            _core.run(doc, "open_project", path=path)
        else:
            _core.run(doc, "open", **_args(path=path, unit=unit))
        return doc

    def save(self, path: PathLike, **options: Any) -> str:
        """Save or export by file name: .sfproj (the whole project), .obj, .glb/.gltf, .usda/.usdz/.usd, .fbx, .ply,
        .stl. `options` go to the command (e.g. lods=3 for FBX, height_mm=80 for STL). The file is replaced in one
        step: a failure never leaves half of it. Returns the command's message."""
        command = _WRITERS.get(_suffix(path))
        if command is None:
            raise ValueError(f"save: no format for '{_suffix(path) or path}' (one of {', '.join(sorted(_WRITERS))})")
        return _core.run(self, command, path=path, **options)

    export = save

    def export_lods(self, path: PathLike, levels: int = 4, *, ratio: float | None = None) -> str:
        """LOD chain: `levels` files next to `path`, each `ratio` (default 0.5) of the one before."""
        return _core.run(self, "export_lods", **_args(path=path, levels=levels, ratio=ratio))

    # --- making and changing the mesh ---

    def new_sphere(self, subdivisions: int = 5) -> str:
        """Replace the scene with a sphere (subdivisions 5: 20 480 triangles)."""
        return _core.run(self, "new_sphere", subdivisions=subdivisions)

    def stroke(
        self,
        brush: str,
        points: ArrayLike,
        *,
        radius: float | None = None,
        strength: float | None = None,
        view: ArrayLike | None = None,
        pressure: float | None = None,
        invert: bool | None = None,
        curve: bool | None = None,
        spacing: float | None = None,
        symmetry: str | None = None,
        seed: int | None = None,
    ) -> str:
        """One brush stroke along `points` ((K, 3) world positions; one point = one dab), each projected onto the
        surface along `view` (default (0, 0, -1): seen from the front). `brush` is a built-in brush (Clay, Crease,
        Inflate, Smooth, ...), a brush from the library, or a detail preset (Detail Crease, Detail Pores, ...).
        `radius` is in scene units (default 4% of the model's size), `strength` 0-1. Same input, same result."""
        return _core.run(
            self,
            "stroke",
            **_args(
                brush=brush,
                points=_points(points),
                radius=radius,
                strength=strength,
                view=None if view is None else _vec3(view, "view"),
                pressure=pressure,
                invert=invert,
                curve=curve,
                spacing=spacing,
                symmetry=symmetry,
                seed=seed,
            ),
        )

    def remesh(self, resolution: int | None = None, *, keep_masked: bool | None = None,
               keep_texture: bool | None = None) -> str:
        """Rebuild the active Part as an even, watertight surface (voxel resolution, default 256)."""
        return _core.run(self, "remesh", **_args(resolution=resolution, keep_masked=keep_masked, keep_texture=keep_texture))

    def decimate(self, ratio: float | None = None, *, triangles: int | None = None,
                 keep_masked: bool | None = None) -> str:
        """Fewer triangles, same shape: keep `ratio` of them, or reduce to `triangles`."""
        if ratio is not None and triangles is not None:
            raise ValueError("decimate: give ratio or triangles, not both")
        return _core.run(self, "decimate", **_args(ratio=ratio, triangles=triangles, keep_masked=keep_masked))

    def subdivide(self, levels: int = 1) -> str:
        """Smooth subdivision, 4x the triangles per level, without keeping the lower levels (see divide)."""
        return _core.run(self, "subdivide", levels=levels)

    # --- subdivision levels ---

    def divide(self, *, smooth: bool = True) -> str:
        """Add a subdivision level above the highest one and go to it; the lower levels stay to step back to."""
        return _core.run(self, "subdiv_divide", smooth=smooth)

    @property
    def subdivision_level(self) -> int:
        """The level shown and sculpted, 1 (lowest) to subdivision_levels; 0 without levels. Set it to change level."""
        level: int = _core.Document.subdivision_level.__get__(self)
        return level

    @subdivision_level.setter
    def subdivision_level(self, level: int) -> None:
        count = self.subdivision_levels
        if not 1 <= level <= count:
            raise IndexError(f"subdivision level {level}: the Part has {count} level(s)")
        _core.run(self, "subdiv_level", level=level)

    # --- masks ---

    def clear_mask(self) -> None:
        """Unmask everything."""
        _core.run(self, "mask_clear")

    def invert_mask(self) -> None:
        """Masked becomes free and free masked."""
        _core.run(self, "mask_invert")

    # --- Parts ---

    def add_part(self, kind: str | None = None, *, path: PathLike | None = None, name: str | None = None,
                 size: float | None = None, unit: str | None = None) -> int:
        """Add a Part: a primitive (`kind`: "sphere", "cube", ...) or a model file (`path`). It becomes the active
        Part; returns its index."""
        if (kind is None) == (path is None):
            raise ValueError("add_part: give a kind or a path")
        _core.run(self, "part_append", **_args(type=kind, path=path, name=name, size=size, unit=unit))
        return self.active_part

    def rename_part(self, part: int | str, to: str) -> None:
        """Rename a Part, given by index or name."""
        _core.run(self, "part_rename", to=to, **self._which(part))

    def remove_part(self, part: int | str) -> None:
        """Delete a Part, given by index or name."""
        _core.run(self, "part_delete", **self._which(part))

    def part_names(self) -> list[str]:
        """The Parts' names in list order."""
        return [p["name"] for p in self.parts]

    def _which(self, part: int | str) -> dict[str, Any]:
        if isinstance(part, str):
            if part not in self.part_names():
                raise KeyError(f"no Part named {part!r}")
            return {"name": part}
        if not 0 <= part < len(self.parts):
            raise IndexError(f"no Part {part}")
        return {"index": part}


def command(name: str) -> _core.CommandInfo:
    """One command's description and parameters, or KeyError."""
    for c in _core.commands():
        if c["name"] == name:
            return c
    raise KeyError(f"no command {name!r}")
