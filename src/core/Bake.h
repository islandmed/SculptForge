// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Map baking: the detail of a high-poly sculpt (one or more SubTools) captured as images on the UVs of a
// low-poly SubTool, for game engines and renderers. Maps: tangent-space normal (MikkTSpace, core/Tangents.h,
// so Blender, Unreal, Unity and Godot show it right), object-space (= world) normal, displacement, ambient
// occlusion, cavity, colour (polypaint or the source's painted texture), and the Mesh Maps texturing reads:
// curvature (convex edges bright, concave dark, from the surface's mean curvature at a set radius), thickness
// (how far rays travel inside the surface), position (in the bounding box, 16-bit) and ID (a flat colour per
// polygroup or per Part).
//
// Sources: other Parts (the classic high-to-low bake), or the active Part's own top subdivision level baked onto
// its lowest level, or its micro level onto its base (bakeLevelSources). UVs outside 0..1 bake as UDIM tiles,
// one set of images per tile (<name>_<map>.1001.png, 1002, ... 1011 for the second row).
//
// How it works, as xNormal and Substance do it: every texel sample on the low-poly surface casts a ray along
// its smooth (interpolated) normal through a "cage" from `front` in front of the surface to `rear` behind it,
// and takes the high-poly hit nearest to the low-poly surface. Distances are fractions of the low-poly's
// bounding-box diagonal; "auto" measures how far the high-poly strays from the low-poly first. Texels are
// supersampled (samples x samples rays each), and `padding` texels around each UV island are filled from the
// island's edge so seams don't show once mipmapped. The work runs on every core, tile by tile, with progress
// and cancel, over a BVH built for the bake (thread-safe, unlike the brush BVH).
//
// Rows are bottom-up (row 0 is v = 0), as PaintTexture's; saving flips them.
#pragma once

#include "core/Commands.h"
#include "core/Image.h"
#include "core/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sf {

enum class BakeMap : int { Normal, ObjectNormal, Displacement, AmbientOcclusion, Cavity, Color, Curvature, Thickness, Position, Id };
constexpr int kBakeMapCount = 10;
constexpr uint32_t bakeBit(BakeMap m) { return 1u << int(m); }
const char* bakeMapId(BakeMap m);    // "normal", "object_normal", "displacement", "ao", "cavity", "color",
                                     // "curvature", "thickness", "position", "id"
const char* bakeMapLabel(BakeMap m); // "Normal (tangent)", ...
// A comma-separated list of ids ("normal,ao", or "all"); false + error naming the bad entry.
bool parseBakeMaps(const std::string& list, uint32_t& maps, std::string& error);

struct BakeSettings {
    int size = 2048;                       // square map, 512..8192
    uint32_t maps = bakeBit(BakeMap::Normal);
    bool autoDistance = true;              // measure front / rear from the meshes (they are reported back)
    float front = 0.02f, rear = 0.02f;     // ray range in front of / behind the low-poly, x bounding-box diagonal
    int samples = 2;                       // supersampling: samples x samples rays per texel (1..4)
    int padding = 8;                       // texels filled around each UV island (0..64)
    int aoRays = 48;                       // per texel
    float aoDistance = 0.05f;              // how far occluders count, x bounding-box diagonal
    bool flipGreen = false;                // DirectX-style normal map (green = down), as Unreal expects natively
    float displacementScale = 0.0f;        // world units that map to white in the PNG; 0 = the largest distance found
    int threads = 0;                       // 0 = every core
    int thicknessRays = 24;                // per texel
    float thicknessDistance = 0.1f;        // rays longer than this count as thick, x bounding-box diagonal
    float curvatureRadius = 0.02f;         // a bend of this radius (x diagonal) is fully bright / dark
    enum class IdSource : uint8_t { Polygroups, Parts };
    IdSource idSource = IdSource::Polygroups;
};

struct BakeResult {
    int width = 0, height = 0;
    uint32_t maps = 0;
    bool flipGreen = false;
    // Present for the maps baked. RGB8 / grey8, rows bottom-up.
    std::vector<uint8_t> normal, objectNormal, color, ao, cavity, curvature, thickness, id;
    std::vector<uint16_t> position;  // RGB16: the point in [positionMin, positionMax]
    std::vector<float> displacement; // world units along the low-poly normal, + = the high-poly is outside
    std::vector<uint8_t> coverage;   // 1 = inside a UV island, 2 = padding, 0 = empty
    std::vector<glm::vec4> tangents; // MikkTSpace tangent + sign per low-poly corner (for previews)
    float front = 0, rear = 0;       // ray range used (world units)
    float diagonal = 0;              // the low-poly's bounding-box diagonal
    float displacementMin = 0, displacementMax = 0; // range found (world units)
    float displacementScale = 0;     // world units at white / black of the 16-bit PNG (mid-grey = 0)
    glm::vec3 positionMin{0.0f}, positionMax{0.0f}; // the box the position map spans
    int udim = 0;                    // the UDIM tile this result is (bakeMapsUdim), 0 = plain 0..1 UVs
    size_t islandTexels = 0, missedTexels = 0; // texels inside islands, and those whose rays found nothing
    size_t sourceTriangles = 0;
    double seconds = 0, bvhSeconds = 0;
    bool has(BakeMap m) const { return (maps & bakeBit(m)) != 0; }
    float missRatio() const { return islandTexels ? float(missedTexels) / float(islandTexels) : 0.0f; }
};

// Progress 0..1; return false to cancel (bakeMaps then fails with "Cancelled").
using BakeProgress = std::function<bool(float)>;

// Bakes `sources` (meshes, hidden triangles skipped) onto the UVs of `low`. Needs UVs on `low`, valid normals
// on every mesh. Colour comes from each source's texture when it has one, else its polypaint.
bool bakeMaps(const Mesh& low, const std::vector<const Mesh*>& sources, const BakeSettings& settings, BakeResult& result,
              std::string& error, const BakeProgress& progress = {});

// The UDIM tiles (1001 + u + 10 v) the low-poly's triangles lie in (by their UV centres), in order; empty when
// every triangle is inside 0..1 (a plain bake).
std::vector<int> udimTiles(const Mesh& low);
// bakeMaps once per UDIM tile of `low` (or once, as a plain bake, without UDIMs): each result has its `udim`.
// Progress covers all tiles.
bool bakeMapsUdim(const Mesh& low, const std::vector<const Mesh*>& sources, const BakeSettings& settings,
                  std::vector<BakeResult>& results, std::string& error, const BakeProgress& progress = {});

// The meshes of a bake from the Part's own levels: `low` its lowest subdivision level and `high` its top one
// (source = "subdivision"), or `low` the Part itself and `high` its micro level baked to triangles
// (source = "micro"). False + error when the Part has no such level or its low mesh has no UVs.
bool bakeLevelSources(const Mesh& part, const std::string& source, Mesh& low, Mesh& high, std::string& error);

// A baked map as a top-down 8-bit image (displacement mid-grey = 0, +/- displacementScale at white / black).
Image bakeMapImage(const BakeResult& result, BakeMap map);
// The displacement as 16-bit grey (top-down; 32768 = 0) and as floats in world units (top-down).
std::vector<uint16_t> displacement16(const BakeResult& result);

// File name of a map: "<base>_normal.png", "<base>_normal_object.png", "<base>_displacement.png" (+ .exr),
// "<base>_ao.png", "<base>_cavity.png", "<base>_color.png", "<base>_curvature.png", "<base>_thickness.png",
// "<base>_position.png" (16-bit), "<base>_id.png"; with a UDIM tile "<base>_normal.1001.png".
std::string bakeFileName(const std::string& base, BakeMap map, const char* extension = ".png", int udim = 0);
// Writes every baked map into `folder` (created if needed); `written` gets the paths. Displacement goes out as
// a 16-bit PNG and, with `exr`, as a 32-bit float EXR in world units.
bool saveBakeMaps(const BakeResult& result, const std::string& folder, const std::string& base, bool exr,
                  std::vector<std::string>& written, std::string& error);

// One line for people: what was baked, the ranges, the displacement scale and the time.
std::string bakeSummary(const BakeResult& result);

// The SubTools a "source" argument names for baking onto the active SubTool: "all" (or empty) = every visible
// SubTool but the active one, "visible" likewise, otherwise a SubTool name. False + error when none qualify.
bool bakeSourceSubtools(const Document& doc, const std::string& source, std::vector<int>& indices, std::string& error);
// Where maps go when no folder is given: the project's folder, else the current folder.
std::string defaultBakeFolder(const Document& doc);

// Called after the bake command succeeds, with the files it wrote (the app shows the normal map on the model).
using BakeListener =
    std::function<void(const Document&, std::shared_ptr<const BakeResult>, const std::vector<std::string>& files)>;
void setBakeListener(BakeListener listener);
void notifyBakeListener(const Document& doc, std::shared_ptr<const BakeResult> result, const std::vector<std::string>& files);

// bake
void registerBakeCommands(CommandRegistry& registry);

} // namespace sf
