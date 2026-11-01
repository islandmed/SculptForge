# SculptForge - ComfyUI nodes: the node classes.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Meshes flow between SculptForge nodes as SF_MESH: a small object holding the path of a
# working file (a .sfproj project, so face groups, vertex paint and sculpt layers survive every
# step). Every mesh input also accepts ComfyUI's core MESH type (Hunyuan3D, TRELLIS, Get 3D
# Components...), and "SculptForge to MESH" goes the other way, so core and SculptForge
# nodes chain freely.
import logging
import os
import re
import uuid

import numpy as np

from . import cli, meshio, paths

log = logging.getLogger("SculptForge")

CATEGORY = "SculptForge"
SF_MESH = "SF_MESH"
MESH_IN = "SF_MESH,MESH"  # union type: SculptForge meshes or ComfyUI core MESH

EXPORT_COMMANDS = {
    "glb": "export_glb",
    "fbx": "export_fbx",
    "usda": "export_usd",
    "usdz": "export_usd",
    "obj": "save_obj",
    "stl": "export_stl",
    "ply": "export_ply",
}


class SFMesh:
    """A mesh on disk that sculptforge-cli can open (normally a .sfproj in ComfyUI's temp folder)."""

    def __init__(self, path, info=""):
        self.path = path
        self.info = info

    def __repr__(self):
        return f"SFMesh({self.path!r})"


def _temp(ext, stem=None):
    """A new working file. Give a stem when SculptForge names the Part after the file."""
    if not stem:
        return os.path.join(paths.work_dir(), f"{uuid.uuid4().hex}{ext}")
    folder = os.path.join(paths.work_dir(), uuid.uuid4().hex)
    os.makedirs(folder)
    return os.path.join(folder, stem + ext)


def _source(mesh):
    """Path of a file sculptforge-cli can open for an SF_MESH or core MESH input."""
    if isinstance(mesh, SFMesh):
        if not os.path.isfile(mesh.path):
            raise cli.SculptForgeError(
                f"The working file {mesh.path} is gone (ComfyUI clears its temp folder on restart); "
                "re-run the nodes before this one.")
        return mesh.path
    if meshio.is_comfy_mesh(mesh):
        if mesh.vertices.shape[0] > 1:
            log.warning("SculptForge: the MESH batch has %d items; using the first", mesh.vertices.shape[0])
        ply = _temp(".ply", "ComfyUI_mesh")
        meshio.comfy_mesh_to_ply(mesh, ply)
        return ply
    raise TypeError(f"Expected a SculptForge mesh or a ComfyUI MESH, got {type(mesh).__name__}")


def _process(mesh, script):
    """Open the mesh, run the script, save the result as a new working project."""
    out = _temp(".sfproj")
    stdout = cli.run(f"open path=$IN; {script}; save_project path=$OUT", {"IN": _source(mesh), "OUT": out})
    lines = [l for l in stdout.strip().splitlines() if l.strip()]
    info = lines[-2].split(": ", 1)[-1] if len(lines) >= 2 else ""
    log.info("SculptForge: %s", info)
    return SFMesh(out, info)


def _create(script, variables):
    """Run a script that creates a scene (image_inflate...) and save it as a working project."""
    out = _temp(".sfproj")
    stdout = cli.run(f"{script}; save_project path=$OUT", dict(variables, OUT=out))
    info = [l for l in stdout.strip().splitlines() if l.strip()][0].split(": ", 1)[-1]
    log.info("SculptForge: %s", info)
    return SFMesh(out, info)


def _image_to_png(image, mask=None):
    """Save the first image of a ComfyUI IMAGE batch (B, H, W, C floats) as PNG; a MASK becomes alpha."""
    from PIL import Image

    arr = image[0] if getattr(image, "ndim", 3) == 4 else image
    arr = arr.detach().cpu().numpy() if hasattr(arr, "detach") else np.asarray(arr)
    rgba = np.clip(arr * 255.0 + 0.5, 0, 255).astype(np.uint8)
    if rgba.shape[-1] == 3 and mask is not None:
        m = mask[0] if getattr(mask, "ndim", 2) == 3 else mask
        m = m.detach().cpu().numpy() if hasattr(m, "detach") else np.asarray(m)
        # LoadImage's MASK is 1 where the picture is transparent; an all-zero mask means "no alpha".
        if m.shape == rgba.shape[:2] and m.max() > 0:
            alpha = np.clip((1.0 - m) * 255.0 + 0.5, 0, 255).astype(np.uint8)
            rgba = np.concatenate([rgba, alpha[..., None]], axis=-1)
    if rgba.shape[-1] == 1:
        rgba = rgba[..., 0]
    path = _temp(".png")
    Image.fromarray(rgba).save(path)
    return path


def _output_ui(saved):
    """UI payload listing saved files the way ComfyUI's own 3D save node does ("3d" key)."""
    return {"3d": [{"filename": f, "subfolder": s, "type": "output"} for f, s in saved]}


# ---------------------------------------------------------------- source nodes


class SculptForgeLoadMesh:
    DESCRIPTION = ("Load a model file (OBJ, FBX, GLB/glTF, PLY, STL, USD/USDZ, ZBrush .GoZ files or a SculptForge "
                   ".sfproj) for the SculptForge nodes. Relative paths are looked up in ComfyUI's input folder.")
    CATEGORY = CATEGORY
    RETURN_TYPES = (SF_MESH,)
    RETURN_NAMES = ("mesh",)
    FUNCTION = "load"

    @classmethod
    def INPUT_TYPES(cls):
        return {"required": {"path": ("STRING", {"default": "", "tooltip": "Model file path (absolute, or "
                                                 "relative to the ComfyUI input folder, e.g. 3d/rock.glb)."})}}

    @classmethod
    def IS_CHANGED(cls, path):
        try:
            return os.path.getmtime(paths.resolve_input(path))
        except (OSError, ValueError):
            return float("nan")

    def load(self, path):
        full = paths.resolve_input(path)
        if not os.path.isfile(full):
            raise cli.SculptForgeError(f"Model file not found: {full}")
        return (_create("open path=$IN", {"IN": full}),)


class SculptForgeInflateImage:
    DESCRIPTION = ("Inflate a sketch or silhouette into a rounded, closed 3D base mesh (no GPU, a second or two). "
                   "Works best with a transparent background (connect LoadImage's MASK) or a plain backdrop.")
    CATEGORY = CATEGORY
    RETURN_TYPES = (SF_MESH,)
    RETURN_NAMES = ("mesh",)
    FUNCTION = "inflate"

    @classmethod
    def INPUT_TYPES(cls):
        return {
            "required": {
                "image": ("IMAGE",),
                "thickness": ("FLOAT", {"default": 1.0, "min": 0.1, "max": 3.0, "step": 0.05,
                                        "tooltip": "1 = round, lower = flatter."}),
                "resolution": ("INT", {"default": 160, "min": 32, "max": 512, "step": 8,
                                       "tooltip": "Detail along the long side."}),
                "subdivisions": ("INT", {"default": 1, "min": 0, "max": 3, "tooltip": "Smoothing passes."}),
            },
            "optional": {
                "mask": ("MASK", {"tooltip": "LoadImage's MASK output: the transparent area is background."}),
            },
        }

    def inflate(self, image, thickness, resolution, subdivisions, mask=None):
        png = _image_to_png(image, mask)
        script = (f"image_inflate path=$IMG thickness={thickness:g} resolution={int(resolution)} "
                  f"subdivisions={int(subdivisions)}")
        return (_create(script, {"IMG": png}),)


class SculptForgeImageRelief:
    DESCRIPTION = ("Turn an image into a sculpted relief: light areas rise, dark areas stay low "
                   "(logos, coins, emblems, height maps).")
    CATEGORY = CATEGORY
    RETURN_TYPES = (SF_MESH,)
    RETURN_NAMES = ("mesh",)
    FUNCTION = "relief"

    @classmethod
    def INPUT_TYPES(cls):
        return {"required": {
            "image": ("IMAGE",),
            "depth": ("FLOAT", {"default": 0.12, "min": 0.01, "max": 1.0, "step": 0.01,
                                "tooltip": "Relief height as a fraction of the width."}),
            "resolution": ("INT", {"default": 384, "min": 32, "max": 2048, "step": 16}),
            "invert": ("BOOLEAN", {"default": False, "tooltip": "Make dark areas high instead."}),
        }}

    def relief(self, image, depth, resolution, invert):
        png = _image_to_png(image)
        script = (f"image_relief path=$IMG depth={depth:g} resolution={int(resolution)} "
                  f"invert={'true' if invert else 'false'}")
        return (_create(script, {"IMG": png}),)


# ---------------------------------------------------------------- processing nodes


class SculptForgeVoxelRemesh:
    DESCRIPTION = ("Voxel remesh (similar to ZBrush's DynaMesh): rebuild the mesh as an even, watertight sculpting surface, "
                   "merging overlapping parts and fixing broken scans or AI-generated models.")
    CATEGORY = CATEGORY
    RETURN_TYPES = (SF_MESH,)
    RETURN_NAMES = ("mesh",)
    FUNCTION = "remesh"

    @classmethod
    def INPUT_TYPES(cls):
        return {"required": {
            "mesh": (MESH_IN,),
            "resolution": ("INT", {"default": 200, "min": 32, "max": 512, "step": 8,
                                   "tooltip": "Voxels across the longest side; higher keeps more detail."}),
        }}

    def remesh(self, mesh, resolution):
        return (_process(mesh, f"remesh resolution={int(resolution)}"),)


class SculptForgeDecimate:
    DESCRIPTION = "Reduce the triangle count while preserving shape (quadric edge collapse)."
    CATEGORY = CATEGORY
    RETURN_TYPES = (SF_MESH,)
    RETURN_NAMES = ("mesh",)
    FUNCTION = "decimate"

    @classmethod
    def INPUT_TYPES(cls):
        return {"required": {
            "mesh": (MESH_IN,),
            "ratio": ("FLOAT", {"default": 0.5, "min": 0.01, "max": 1.0, "step": 0.01,
                                "tooltip": "Fraction of triangles to keep."}),
            "triangles": ("INT", {"default": 0, "min": 0, "max": 100_000_000,
                                  "tooltip": "Exact target triangle count; 0 uses the ratio."}),
        }}

    def decimate(self, mesh, ratio, triangles):
        args = f"triangles={int(triangles)}" if triangles > 0 else f"ratio={ratio:g}"
        return (_process(mesh, f"decimate {args}"),)


class SculptForgeSubdivide:
    DESCRIPTION = "Smoothly subdivide the mesh (Loop subdivision). Each level multiplies the triangle count by 4."
    CATEGORY = CATEGORY
    RETURN_TYPES = (SF_MESH,)
    RETURN_NAMES = ("mesh",)
    FUNCTION = "subdivide"

    @classmethod
    def INPUT_TYPES(cls):
        return {"required": {"mesh": (MESH_IN,), "levels": ("INT", {"default": 1, "min": 1, "max": 4})}}

    def subdivide(self, mesh, levels):
        return (_process(mesh, f"subdivide levels={int(levels)}"),)


class SculptForgeToMesh:
    DESCRIPTION = ("Convert a SculptForge mesh to ComfyUI's core MESH type (with vertex colours and normals), "
                   "for Save 3D Model, Preview 3D and the other core 3D nodes.")
    CATEGORY = CATEGORY
    RETURN_TYPES = ("MESH",)
    RETURN_NAMES = ("mesh",)
    FUNCTION = "convert"

    @classmethod
    def INPUT_TYPES(cls):
        return {"required": {"mesh": (SF_MESH,)}}

    def convert(self, mesh):
        ply = _temp(".ply")
        cli.run("open path=$IN; export_ply path=$OUT", {"IN": _source(mesh), "OUT": ply})
        return (meshio.ply_to_comfy_mesh(ply),)


# ---------------------------------------------------------------- output nodes


class SculptForgeExport:
    DESCRIPTION = ("Export to the ComfyUI output folder: GLB (game engines, web), FBX (Unreal, Unity, Maya), "
                   "USDA/USDZ (VFX pipelines, Apple AR), OBJ, STL (3D printing) or PLY (vertex colours).")
    CATEGORY = CATEGORY
    RETURN_TYPES = ("STRING",)
    RETURN_NAMES = ("file_path",)
    OUTPUT_NODE = True
    FUNCTION = "export"

    @classmethod
    def INPUT_TYPES(cls):
        return {"required": {
            "mesh": (MESH_IN,),
            "format": (list(EXPORT_COMMANDS), {"default": "glb"}),
            "filename_prefix": ("STRING", {"default": "3d/SculptForge"}),
        }}

    def export(self, mesh, format, filename_prefix):
        command = EXPORT_COMMANDS[format]
        folder, filename, counter, subfolder = paths.save_path(filename_prefix)
        name = f"{filename}_{counter:05}_.{format}"
        full = os.path.join(folder, name)
        cli.run(f"open path=$IN; {command} path=$OUT", {"IN": _source(mesh), "OUT": full})
        if not os.path.isfile(full):
            raise cli.SculptForgeError(f"SculptForge reported success but {full} was not written")
        log.info("SculptForge: exported %s", full)
        return {"ui": _output_ui([(name, subfolder)]), "result": (full,)}


class SculptForgeLODChain:
    DESCRIPTION = ("Generate a game LOD chain and save each level as a GLB (<name>_LOD0.glb, _LOD1.glb...) "
                   "in the ComfyUI output folder.")
    CATEGORY = CATEGORY
    RETURN_TYPES = ("STRING",)
    RETURN_NAMES = ("file_paths",)
    OUTPUT_NODE = True
    FUNCTION = "export"

    @classmethod
    def INPUT_TYPES(cls):
        return {"required": {
            "mesh": (MESH_IN,),
            "levels": ("INT", {"default": 4, "min": 1, "max": 8, "tooltip": "LODs including LOD0."}),
            "ratio": ("FLOAT", {"default": 0.5, "min": 0.1, "max": 0.9, "step": 0.05,
                                "tooltip": "Triangle ratio between consecutive LODs."}),
            "filename_prefix": ("STRING", {"default": "3d/SculptForge_lods"}),
        }}

    def export(self, mesh, levels, ratio, filename_prefix):
        folder, filename, counter, subfolder = paths.save_path(filename_prefix)
        base = f"{filename}_{counter:05}"
        cli.run(f"open path=$IN; export_lods path=$OUT levels={int(levels)} ratio={ratio:g}",
                {"IN": _source(mesh), "OUT": os.path.join(folder, base + ".glb")})
        names = [f"{base}_LOD{i}.glb" for i in range(int(levels))]
        missing = [n for n in names if not os.path.isfile(os.path.join(folder, n))]
        if missing:
            raise cli.SculptForgeError(f"LOD files were not written: {', '.join(missing)}")
        full = [os.path.join(folder, n) for n in names]
        return {"ui": _output_ui([(n, subfolder) for n in names]), "result": ("\n".join(full),)}


class SculptForgeMeshInfo:
    DESCRIPTION = "Report vertex and triangle counts, bounds and Parts of a mesh."
    CATEGORY = CATEGORY
    RETURN_TYPES = ("STRING",)
    RETURN_NAMES = ("info",)
    OUTPUT_NODE = True
    FUNCTION = "info"

    @classmethod
    def INPUT_TYPES(cls):
        return {"required": {"mesh": (MESH_IN,)}, "hidden": {"unique_id": "UNIQUE_ID"}}

    def info(self, mesh, unique_id=None):
        stdout = cli.run("open path=$IN; mesh_info; subtool_list", {"IN": _source(mesh)})
        lines = [l.split(": ", 1)[-1] if re.match(r"^\w+: ", l) else l for l in stdout.strip().splitlines()[1:]]
        text = "\n".join(lines)
        paths.show_text(text, unique_id)
        return {"ui": {"text": (text,)}, "result": (text,)}


class SculptForgeOpenInApp:
    DESCRIPTION = ("Open the mesh (or an exported file) in the SculptForge desktop app to keep sculpting. "
                   "Only useful when ComfyUI runs on your own computer.")
    CATEGORY = CATEGORY
    RETURN_TYPES = ()
    OUTPUT_NODE = True
    FUNCTION = "open"

    @classmethod
    def INPUT_TYPES(cls):
        return {
            "required": {"enabled": ("BOOLEAN", {"default": True})},
            "optional": {
                "mesh": (MESH_IN,),
                "file_path": ("STRING", {"forceInput": True, "tooltip": "e.g. the file_path of SculptForge Export."}),
            },
        }

    def open(self, enabled, mesh=None, file_path=None):
        if not enabled:
            return {}
        target = file_path.splitlines()[0].strip() if file_path else None
        if not target and mesh is not None:
            # Copy the working file so the app's Save doesn't write into ComfyUI's temp folder.
            target = os.path.join(paths.work_dir(), "SculptForge from ComfyUI.sfproj")
            cli.run("open path=$IN; save_project path=$OUT", {"IN": _source(mesh), "OUT": target})
        if not target:
            raise cli.SculptForgeError("Open in SculptForge needs a mesh or a file_path input.")
        app = cli.launch_app(target)
        log.info("SculptForge: opened %s in %s", target, app)
        return {"ui": {"text": (f"Opened {os.path.basename(target)} in SculptForge",)}}


NODE_CLASS_MAPPINGS = {
    "SculptForgeLoadMesh": SculptForgeLoadMesh,
    "SculptForgeInflateImage": SculptForgeInflateImage,
    "SculptForgeImageRelief": SculptForgeImageRelief,
    "SculptForgeVoxelRemesh": SculptForgeVoxelRemesh,
    "SculptForgeDecimate": SculptForgeDecimate,
    "SculptForgeSubdivide": SculptForgeSubdivide,
    "SculptForgeLODChain": SculptForgeLODChain,
    "SculptForgeExport": SculptForgeExport,
    "SculptForgeMeshInfo": SculptForgeMeshInfo,
    "SculptForgeToMesh": SculptForgeToMesh,
    "SculptForgeOpenInApp": SculptForgeOpenInApp,
}

NODE_DISPLAY_NAME_MAPPINGS = {
    "SculptForgeLoadMesh": "SculptForge Load Mesh",
    "SculptForgeInflateImage": "SculptForge Inflate Image",
    "SculptForgeImageRelief": "SculptForge Image Relief",
    "SculptForgeVoxelRemesh": "SculptForge Voxel Remesh",
    "SculptForgeDecimate": "SculptForge Decimate",
    "SculptForgeSubdivide": "SculptForge Subdivide",
    "SculptForgeLODChain": "SculptForge LOD Chain",
    "SculptForgeExport": "SculptForge Export",
    "SculptForgeMeshInfo": "SculptForge Mesh Info",
    "SculptForgeToMesh": "SculptForge to MESH",
    "SculptForgeOpenInApp": "Open in SculptForge",
}
