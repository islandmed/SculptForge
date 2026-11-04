// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Make sculptable (Splats palette): one click from a Gaussian-splat capture to a clean, sculptable,
// animation-ready Part with the capture's colours.
//
//  1. Surface. Every Gaussian adds its opacity-weighted density to a voxel grid (in parallel, in z slabs, so
//     the sum is the same with any number of threads). Cells over the density threshold are the shell; gaps
//     up to a few cells close; cells boxed in by the shell along five of the six axis directions count as
//     inside, which seals big holes such as the open bottom of a bust scanned on a table; the outside is
//     flood-filled from the grid border and only the largest solid piece is kept (floaters and background
//     specks go). An optional crop box drops everything outside it first. Surface nets + Taubin smoothing
//     give a watertight surface ("the scan").
//  2. Topology. QRemesh (core/QRemesh.h) rebuilds the scan as quads at a preset density (game, film, sculpt
//     base). Face-aware: when the scan is a head, landmarks (eye corners, nose tip, mouth corners, chin) are
//     found on a depth + colour image of the scan seen from the front, and turned into QRemesh Guides: rings
//     round the eyes and the mouth, the nasolabial lines, mirrored across the face so the loops are symmetric.
//     The user can review and nudge the landmarks before remeshing (the app's Review guides step).
//  3. Colour. Every vertex (vertex paint) or texel (Auto UV + a texture) takes the colour of the splats on the
//     surface around it, weighted by opacity and distance, with splats behind the surface faded out.
//     Optionally the quad mesh is divided into subdivision levels and the scan's detail projected onto the top
//     level (as ProjectAll), so the user sculpts with levels.
//
// All of it runs on a worker thread in the app (progress and Cancel); the splat_to_mesh command runs it inline.
#pragma once

#include "core/Commands.h"
#include "core/Mesh.h"
#include "core/QRemesh.h"
#include "core/QRemeshGuides.h"
#include "core/Splats.h"

#include <glm/glm.hpp>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace sf {

class BVH;
class CommandRegistry;
struct Document;

// Fraction 0..1 of the whole job; return false to cancel.
using SculptableProgress = std::function<bool(float fraction)>;

// --- 1. Surface -------------------------------------------------------------------------------------------

struct SplatSurfaceOptions {
    int resolution = 256;       // Detail: voxels across the subject's longest side (32-768)
    float minOpacity = 0.2f;    // ignore fainter splats
    float keepFraction = 0.98f; // per axis, ignore this fraction's outliers when sizing the grid (1 = all)
    float threshold = 0.3f;     // summed density that counts as surface
    int closeRadius = 2;        // seal gaps up to this many voxels
    bool fillHoles = true;      // cells enclosed along 5 of 6 axis directions are inside (open bottoms, big holes)
    bool keepAllParts = false;  // false: only the largest solid piece (drops floaters)
    int smoothIterations = 6;
    bool crop = false;          // only splats inside cropBox (world space) count
    AABB cropBox;
};

struct SplatSurfaceStats {
    size_t splats = 0;           // in the capture
    size_t used = 0;             // splatted into the grid
    size_t faint = 0, cropped = 0, outliers = 0, huge = 0; // left out, by reason
    glm::ivec3 dims{0};
    float cell = 0.0f;           // voxel size
    size_t solidVoxels = 0;
    size_t floaterVoxels = 0;    // solid voxels in the smaller pieces that were dropped
    int pieces = 0;              // solid pieces found before keeping the largest
    double densitySeconds = 0, solidSeconds = 0, meshSeconds = 0;
};

// The watertight surface of a capture (world space). False with `error` set on failure or cancel.
bool splatSurface(const SplatCloud& cloud, const SplatSurfaceOptions& options, Mesh& out, std::string& error,
                  const SculptableProgress& progress = {}, SplatSurfaceStats* stats = nullptr);

// --- 3. Colour --------------------------------------------------------------------------------------------

// Linear RGB colour of the splats at a surface point. Thread-safe after construction.
class SplatColorSampler {
public:
    // `radius`: how far around a point splats count (world units; about two voxels of the surface).
    SplatColorSampler(const SplatCloud& cloud, float radius, float minOpacity, const AABB* crop = nullptr);
    // Colour at `p` with outward normal `n`; `found` false (and grey) when no splat is near.
    glm::vec3 sample(const glm::vec3& p, const glm::vec3& n, bool* found = nullptr) const;
    size_t size() const { return pos_.size(); }

private:
    glm::vec3 gather(const glm::vec3& p, const glm::vec3& n, float radius, float& weight) const;
    float radius_ = 0.0f, cell_ = 1.0f;
    glm::vec3 origin_{0.0f};
    glm::ivec3 dims_{0};
    std::vector<glm::vec3> pos_, color_; // compacted splats (world position, linear colour), cell order
    std::vector<float> alpha_;
    std::vector<uint32_t> cellStart_;    // CSR: splats of cell c are [cellStart_[c], cellStart_[c + 1])
};

// Sets the vertex paint of every vertex from the splats. Vertices with no splat near take their neighbours' colour;
// returns how many there were.
size_t colorFromSplats(Mesh& mesh, const SplatColorSampler& sampler);
// Bakes the splat colour into a size x size texture on the mesh's UVs (it must have UVs); painting then goes
// into the texture.
void textureFromSplats(Mesh& mesh, const SplatColorSampler& sampler, int size);

// --- 2. Face-aware topology -------------------------------------------------------------------------------

// The face's frame: `front` points out of the face (towards a viewer looking at it), `up` to the top of the head.
struct FaceFrame {
    glm::vec3 front{0.0f, 0.0f, 1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 right() const { return glm::normalize(glm::cross(up, front)); }
    bool orthonormalize(); // false when front and up are (nearly) parallel
};

// Landmarks on the surface (mesh space). Index 0 of a pair is on the -right side (the viewer's left when facing
// the face), 1 on the +right side.
struct FaceLandmarks {
    bool valid = false;
    std::array<glm::vec3, 2> eyeOuter{}, eyeInner{}, mouthCorner{};
    glm::vec3 noseTip{0.0f}, chin{0.0f};
    float confidence = 0.0f; // 0..1, from how face-like the proportions and the depth relief are
    static constexpr int kCount = 8;
    static const char* name(int i); // eye_outer_l, eye_inner_l, eye_inner_r, eye_outer_r, nose_tip, mouth_l, mouth_r, chin
    glm::vec3& point(int i);
    const glm::vec3& point(int i) const;
};

// Looks for a face on the mesh seen from `frame.front` (depth relief and, with vertex paint, darkness of the
// eyes and mouth). False with `why` set when nothing face-like is found.
bool detectFaceLandmarks(const Mesh& mesh, const FaceFrame& frame, FaceLandmarks& out, std::string& why);
// Makes the landmarks mirror images of each other across the face's middle (the plane through the nose tip
// and chin facing `right`), snapped back onto the surface.
void symmetrizeFaceLandmarks(FaceLandmarks& lm, const FaceFrame& frame, const Mesh& mesh, const BVH& bvh);
// QRemesh Guides from landmarks: a loop round each eye and the orbit, a loop round the lips and one round the
// mouth area, and the nasolabial lines (nose wing past the mouth corner), all mirrored. `quadEdge`: the
// expected quad edge length; loops too small to come out as clean rings at that density are left out.
std::vector<QRemeshGuide> faceGuides(const Mesh& mesh, const BVH& bvh, const FaceLandmarks& lm,
                                     const FaceFrame& frame, float quadEdge);
// Landmarks as {"eye_outer_l": [x, y, z], ...} and back (false if any is missing).
std::string faceLandmarksJson(const FaceLandmarks& lm);
bool faceLandmarksFromJson(const std::string& json, FaceLandmarks& lm, std::string& error);

// --- The whole job ----------------------------------------------------------------------------------------

enum class SculptablePreset { Game, Film, SculptBase, Custom };
// Target quads of a preset (Custom: 0).
int sculptablePresetQuads(SculptablePreset preset);
const char* sculptablePresetName(SculptablePreset preset); // game, film, sculpt
bool parseSculptablePreset(const std::string& text, SculptablePreset& out);

enum class SculptableColor { None, VertexPaint, Texture };

struct SculptableOptions {
    SplatSurfaceOptions surface;
    int targetQuads = 6000;     // 0: keep the voxel surface (no QRemesh)
    float adaptive = 0.3f;
    bool symmetry = true;       // QRemesh mirrors across the world axis nearest the face's right (auto plane)
    bool faceGuides = true;     // look for a face and add guides (when no guides are given)
    FaceFrame frame;
    SculptableColor color = SculptableColor::VertexPaint;
    int textureSize = 2048;
    int levels = 0;             // subdivision levels on top of the quad mesh, the scan's detail projected onto them
    uint32_t seed = 1;
};

struct SculptableResult {
    Mesh scan;                  // the watertight voxel surface, with the splat colours as vertex paint
    Mesh mesh;                  // the sculptable result (quads, levels, colour)
    FaceLandmarks landmarks;    // when a face was found (or given)
    std::vector<QRemeshGuide> guides;
    SplatSurfaceStats surface;
    QRemeshStats qremesh;
    size_t uncolored = 0;       // vertices with no splat near
    double colorSeconds = 0, projectSeconds = 0, seconds = 0;
    std::string faceNote;       // why no face guides, or which landmarks were used
    std::string report() const; // one status line
};

// Stage 1: the scan (surface + vertex paint) and, with options.faceGuides, the landmarks and guides.
bool makeSculptableScan(const SplatCloud& cloud, const SculptableOptions& options, SculptableResult& result,
                        std::string& error, const SculptableProgress& progress = {});
// Stage 2: QRemesh `result.scan` along `result.guides`, then levels and colour, into `result.mesh`.
bool finishSculptable(const SplatCloud& cloud, const SculptableOptions& options, SculptableResult& result,
                      std::string& error, const SculptableProgress& progress = {});

// --- Test subjects (no downloads) ---------------------------------------------------------------------------

// A stylised head facing +Z (Y up), about 1.6 wide: nose, eye sockets with painted eyes, brows, lips, chin and
// hair, as a coloured mesh (vertex paint). `landmarks` (optional): where its face landmarks were designed.
Mesh makeTestHead(int subdivisions = 6, FaceLandmarks* landmarks = nullptr);
// A mug-like object with a thin handle (no cavity), coloured.
Mesh makeTestObject(int resolution = 160);

struct TestCaptureOptions {
    size_t count = 200'000;  // surface splats
    size_t floaters = 0;     // small clusters of splats floating around the subject
    size_t interior = 0;     // splats inside the subject with a wrong colour (green), as trained captures have
    bool openBottom = false; // no splats on the lowest 15% (a subject standing on a table)
    uint32_t seed = 7;
};
// The mesh as a capture-like splat cloud: flat surfels in its vertex paint colours (display sRGB), plus the
// options' imperfections.
SplatCloud makeTestCapture(const Mesh& mesh, const TestCaptureOptions& options);

// Adds the result to the scene as Parts named after the capture: "<name> Scan" (hidden; the source to
// ProjectAll from again) when `keepScan`, then "<name>" made active. An untouched starting sphere is hidden.
// Returns the status message.
std::string applySculptable(Document& doc, SculptableResult& result, bool keepScan);
// Stage 1 into the scene: the scan as "<name> Scan", made active, with the face guides. Returns the status message
// (with the landmarks as JSON when a face was found).
std::string applySculptableScan(Document& doc, SculptableResult& result);
// Stage 2 into the scene: the result next to the active scan Part `scanIndex` (hidden, or removed without
// `keepScan`), named like it without " Scan", made active. Returns the status message.
std::string applySculptableFinish(Document& doc, SculptableResult& result, int scanIndex, bool keepScan);

// The options of a splat_to_mesh command line (the app builds its runs from the same arguments, so the console
// shows what it did). False with `error` on a bad value.
bool sculptableOptionsFromArgs(const Args& args, SculptableOptions& options, std::string& error);

// splat_to_mesh, face_guides, splat_test_capture.
void registerSculptableCommands(CommandRegistry& registry);

} // namespace sf
