// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Vector displacement maps (ZBrush VDM brushes, Blender's "VDM brushes"): a square grid of 3D offsets in the
// brush's tangent frame. One dab of the VDM brush (BrushType::Vdm) moves every vertex under the stamp by its
// texel's vector, so a single click can press a horn, an ear curl or a nostril with overhangs, which a height
// (grayscale alpha) can't.
//
// Convention (the same as Blender's VDM Brush Baker and its Draw brush with "Vector Displacement"):
//   - the map covers the brush's square, -1..1 in both directions (1 = the brush radius);
//   - R = X, along the stamp's right; G = Y, toward the stamp's top (the image's top row, the drag direction
//     of DragRect); B = Z, out of the surface along its normal;
//   - values are in brush radii: (0, 0, 0.5) lifts the surface by half the radius.
// Blender bakes the map from a 2 x 2 grid (-1..1) and its brush scales the vector by the radius, so a map from
// Blender stamps the same shape here and ours there. EXR files are read as stored, first scanline = top.
#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class CommandRegistry;
class Mesh;
struct Document;
struct BrushLibraryPaths;

struct VectorDisplacement {
    std::string name = "VDM";
    std::string path; // the .sfvdm / .exr file it came from or is saved to (empty: built-in or unsaved)
    bool builtin = false;
    int width = 0, height = 0;
    std::vector<glm::vec3> vectors; // row-major, row 0 at the top (+Y), tangent space (see above)

    bool empty() const { return vectors.empty(); }
    // u, v in -1..1 (v up). Bilinear; (0, 0, 0) outside the square.
    glm::vec3 sample(float u, float v) const;
    // Largest |vector| (how far the stamp reaches out of its square, in radii).
    float extent() const;
    // Content hash (thumbnail keys, change detection).
    uint64_t hash() const;
};

// How to read the axes of a map made elsewhere (applied in this order: swap, flips, scale).
struct VdmImportOptions {
    bool swapYZ = false; // G and B swapped (Y-up apps)
    bool flipX = false, flipY = false, flipZ = false;
    float scale = 1.0f;
};

// --- Files ---

// OpenEXR (half or float, any compression tinyexr reads: none, RLE, ZIP, ZIPS, PIZ, PXR24, B44 ...). RGB are
// the vector; a one-channel (grayscale) EXR is a plain height map (Z only).
bool readVdmExr(const std::string& path, VectorDisplacement& out, std::string& error, const VdmImportOptions& opt = {});
bool decodeVdmExr(const unsigned char* bytes, size_t size, VectorDisplacement& out, std::string& error,
                  const VdmImportOptions& opt = {});
// 32-bit float RGB, ZIP compressed: what Blender's VDM Brush Baker writes at 32 bits.
bool writeVdmExr(const std::string& path, const VectorDisplacement& vdm, std::string& error);
bool encodeVdmExr(const VectorDisplacement& vdm, std::vector<unsigned char>& out, std::string& error);

// Our own format (the brush library keeps VDMs as .sfvdm): "SFVDM1\n\0", u32 width, u32 height, u32 name
// length, the UTF-8 name, then width x height x 3 little-endian float32 (row 0 at the top). At most 2048 px a side.
bool readVdmFile(const std::string& path, VectorDisplacement& out, std::string& error);
bool writeVdmFile(const std::string& path, const VectorDisplacement& vdm, std::string& error);
// By extension: .sfvdm or .exr.
bool loadVdm(const std::string& path, VectorDisplacement& out, std::string& error, const VdmImportOptions& opt = {});
bool isVdmFile(const std::string& path);

// --- Built-ins (procedural, made in code; CC0): Horn, Ear Curl, Scales, Nostril, Rivet, Stitches, Skin Fold,
// Wrinkle Line, Crack, Rock Ledge ---

const std::vector<std::shared_ptr<const VectorDisplacement>>& builtinVdms();
std::shared_ptr<const VectorDisplacement> builtinVdm(const std::string& name); // case-insensitive; null if unknown
// The generators (exposed for tests; `size` texels a side).
VectorDisplacement makeHornVdm(int size = 256);
VectorDisplacement makeEarCurlVdm(int size = 256);
VectorDisplacement makeScalesVdm(int size = 256);
VectorDisplacement makeNostrilVdm(int size = 256);
VectorDisplacement makeRivetVdm(int size = 256);
VectorDisplacement makeStitchesVdm(int size = 256);
VectorDisplacement makeSkinFoldVdm(int size = 256);
VectorDisplacement makeWrinkleVdm(int size = 256);
VectorDisplacement makeCrackVdm(int size = 256);
VectorDisplacement makeRockLedgeVdm(int size = 256);

// --- Create VDM from a plane (ZBrush: Brush > Create > from the VDM plane; Blender: VDM Brush Baker) ---

// A flat 2 x 2 grid in the XZ plane facing +Y (the top view), `cells` quads a side, with UVs (they record
// where each vertex started, so its offset can be captured after sculpting) and the outer ring masked.
Mesh makeVdmPlane(int cells = 256);
// Rasterizes (position - start) of every triangle of `mesh` into a `size` x `size` map through its UVs (0..1,
// v up). Map axes: X = world +X, Y = world -Z (up in the top view), Z = world +Y. False without UVs.
bool captureVdm(const Mesh& mesh, int size, VectorDisplacement& out, std::string& error);

// --- Library (VDMs live as .sfvdm files next to the .sfbrush files; see core/BrushLibrary.h) ---

// Resets doc.vdms to the built-ins plus the studio and user library .sfvdm files; keeps the current VDM by name.
void loadVdmLibrary(Document& doc, const BrushLibraryPaths& paths, std::vector<std::string>& messages);
// Adds or replaces (by name) `vdm` in doc.vdms and makes it the brush's VDM; returns it.
std::shared_ptr<const VectorDisplacement> addVdm(Document& doc, VectorDisplacement vdm);
// Index of the VDM called `name` in doc.vdms (case-insensitive), or -1.
int findVdm(const Document& doc, const std::string& name);
// "<name>.sfvdm" made safe on every OS.
std::string vdmFileName(const std::string& name);

// vdm_set, vdm_list, vdm_export, vdm_new_plane, vdm_capture, vdm_stamp, vdm_delete.
void registerVdmCommands(CommandRegistry& registry);

} // namespace sf
