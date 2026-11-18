# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
# Type stubs for the compiled module (src/python/Bindings.cpp). Keep them in step with it: tests/python checks that
# every public name in the module is here.
from os import PathLike
from typing import Literal, TypedDict, Union

import numpy as np
from numpy.typing import ArrayLike, NDArray

__version__: str

_Value = Union[bool, int, float, str, PathLike[str]]

class CommandError(RuntimeError):
    """A command failed; the message names the command and says why."""

class PartInfo(TypedDict):
    name: str
    visible: bool
    triangles: int

class SelectionSetInfo(TypedDict):
    name: str
    channels: list[Literal["mask", "facegroups", "paint", "visibility", "uvs", "morph"]]
    level: int
    vertex_count: int
    triangle_count: int
    fits: bool
    morph_target: bool

class SelectionSetChannels(TypedDict, total=False):
    mask: NDArray[np.float32]
    facegroups: NDArray[np.uint32]
    colors: NDArray[np.float32]
    hidden: NDArray[np.uint8]
    uvs: NDArray[np.float32]
    positions: NDArray[np.float32]

class ParamInfo(TypedDict):
    name: str
    type: Literal["bool", "number", "string"]
    description: str
    required: bool

class CommandInfo(TypedDict):
    name: str
    description: str
    params: list[ParamInfo]

class Document:
    """A scene: the sculpt mesh (with layers), Gaussian splats and edit history."""

    def __init__(self) -> None: ...
    @property
    def vertices(self) -> NDArray[np.float32]:
        """Current vertex positions, (N, 3) float32 (a copy)."""
    @property
    def base_vertices(self) -> NDArray[np.float32]:
        """Vertex positions with every layer at zero, (N, 3) float32."""
    @property
    def normals(self) -> NDArray[np.float32]: ...
    @property
    def triangles(self) -> NDArray[np.uint32]:
        """Triangle vertex indices, (M, 3) uint32 (a copy)."""
    @property
    def mask(self) -> NDArray[np.float32] | None:
        """Per-vertex mask, (N,) float32, 0 = free and 1 = fully masked; None if the mesh has no mask."""
    @mask.setter
    def mask(self, value: ArrayLike | None) -> None: ...
    @property
    def colors(self) -> NDArray[np.float32] | None:
        """Per-vertex paint colours, (N, 3) float32 linear RGB in 0..1; None if unpainted."""
    @colors.setter
    def colors(self, value: ArrayLike | None) -> None: ...
    @property
    def facegroups(self) -> NDArray[np.uint32] | None:
        """Per-triangle face group ids, (M,) uint32; None means every triangle is in group 0."""
    @facegroups.setter
    def facegroups(self, value: ArrayLike | None) -> None: ...
    @property
    def hidden(self) -> NDArray[np.uint8] | None:
        """Per-triangle hidden flags, (M,) uint8 (nonzero = hidden); None means all visible."""
    @hidden.setter
    def hidden(self, value: ArrayLike | None) -> None: ...
    @property
    def layers(self) -> list[tuple[str, float]]:
        """Sculpt layers (blend shapes) as (name, weight) tuples."""
    def layer_delta(self, index: int) -> NDArray[np.float32]:
        """Per-vertex offsets of a layer, (N, 3) float32."""
    def set_mesh(self, vertices: ArrayLike, triangles: ArrayLike) -> None:
        """Replace the active Part's mesh with your own (clears undo)."""
    @property
    def splat_count(self) -> int: ...
    @property
    def subdivision_levels(self) -> int:
        """How many subdivision levels the active Part has (0: none)."""
    @property
    def subdivision_level(self) -> int:
        """The level shown and sculpted, 1 to subdivision_levels; 0 without levels."""
    @property
    def parts(self) -> list[PartInfo]:
        """The Parts (separate meshes) in list order."""
    @property
    def active_part(self) -> int:
        """Index of the Part that vertices, mask, layers... and editing commands refer to."""
    @active_part.setter
    def active_part(self, value: int | str) -> None: ...
    @property
    def selection_sets(self) -> list[SelectionSetInfo]:
        """The active Part's selection sets."""
    def selection_set(self, name: str) -> SelectionSetChannels:
        """A selection set's channels at the current subdivision level, only those the set holds."""
    def set_selection_set(
        self,
        name: str,
        *,
        mask: ArrayLike | None = None,
        facegroups: ArrayLike | None = None,
        colors: ArrayLike | None = None,
        hidden: ArrayLike | None = None,
        uvs: ArrayLike | None = None,
        positions: ArrayLike | None = None,
    ) -> str:
        """Store (or replace) a selection set on the active Part from arrays laid out for the current level."""

def run(document: Document, command: str, **kwargs: _Value) -> str:
    """Run a command on a document, e.g. run(doc, 'remesh', resolution=256); returns its message."""

def run_script(document: Document, script: str) -> list[str]:
    """Run a script in sculptforge-cli syntax: 'new_sphere subdivisions=5; export_glb path=out.glb'."""

def commands() -> list[CommandInfo]:
    """Every available command with its parameters."""

def tool_schema() -> str:
    """Commands as LLM tool definitions (JSON)."""

