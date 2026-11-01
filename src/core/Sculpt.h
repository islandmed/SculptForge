// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/BrushRecipe.h"
#include "core/Dyntopo.h"
#include "core/Mesh.h"
#include "core/Undo.h"

#include <chrono>
#include <string>
#include <vector>

namespace sf {

struct BrushAlpha;
struct VectorDisplacement;

enum class BrushType {
    Standard, Clay, Smooth, Move, Inflate, Crease, Flatten, Pinch,
    DamStandard, // sharp V-crease with the sides pinched toward the stroke
    ClayBuildup, // square-ish, flat-topped clay
    SnakeHook,   // pulls the region along the stroke (strokeDelta), tapering
    Trim,        // Trim Dynamic: a plane cuts away everything above it
    Layer,       // raises to one fixed height per stroke (overlaps never stack)
    Nudge,       // slides the surface sideways along the stroke
    Vdm,         // vector displacement: stamps DabParams::vdm (a horn, an ear...) with overhangs (core/Vdm.h)
    Count
};

const char* brushName(BrushType type);

struct DabParams {
    glm::vec3 center{0.0f};             // surface point under the cursor
    glm::vec3 viewDir{0.0f, 0.0f, -1.0f}; // camera ray direction (into the scene)
    float radius = 0.1f;                // world units
    float strength = 0.5f;              // 0..1
    float pressure = 1.0f;              // tablet pressure, 0..1
    bool invert = false;
    bool frontFacesOnly = false;
    glm::vec3 strokeDelta{0.0f}; // world-space movement since the previous dab (recipe, SnakeHook, Nudge, DamStandard)
    const BrushAlpha* alpha = nullptr;   // stamp shaping the falloff (null/empty = round)
    glm::vec3 alphaUp{0.0f, 1.0f, 0.0f}; // world direction of the alpha's top edge (screen up, or rotated)
    float focal = 0.0f;                  // ZBrush Focal Shift, -1..1 (see brushFalloff); recipes keep their own tip
    const VectorDisplacement* vdm = nullptr; // the Vdm brush's map (null = the built-in Horn); top edge = alphaUp
};

struct Symmetry {
    bool x = true, y = false, z = false;
};

// --- Shared brush helpers (every brush, including future paint/mask brushes, goes through these) ---

// Strength multiplier for vertex `v`: 0 if every triangle around it is
// hidden, otherwise 1 - mask. Applied centrally by Sculptor::displace, so
// all displacement brushes respect mask and visibility. Paint brushes should
// scale by it too; brushes that edit the mask itself must not.
inline float vertexInfluence(const Mesh& mesh, uint32_t v) {
    if (mesh.hasHidden() && mesh.vertexHidden(v)) return 0.0f;
    return 1.0f - mesh.maskAt(v);
}
// Vertices within `radius` of `center` that a brush may touch (fully hidden ones excluded).
void brushVertices(const Mesh& mesh, const BVH& bvh, const glm::vec3& center, float radius, std::vector<uint32_t>& out);
// Smoothstep brush falloff: 1 at the centre, 0 at `radius`. `focal` is ZBrush's Focal Shift (-1..1): below 0
// the full-strength centre widens (a harder, broader tip), above 0 it narrows to a softer point; 0 is the plain
// smoothstep, unchanged.
float brushFalloff(float dist, float radius, float focal = 0.0f);
// Where brushFalloff drops to one half, as a fraction of the radius (the brush cursor's inner ring).
float focalRingFraction(float focal);

// Where brush time goes (core/Benchmark.h, Help > Performance): milliseconds summed over dabs since the
// counters were last zeroed. kernel = finding the vertices and moving / masking / painting them.
struct SculptTimings {
    double kernelMs = 0.0, normalsMs = 0.0, bvhMs = 0.0;
    double remeshMs = 0.0; // dynamic topology (core/Dyntopo.h)
    uint64_t dabs = 0, vertices = 0;
};
// Milliseconds since `t`, which moves on to now (for SculptTimings).
inline double lapMs(std::chrono::steady_clock::time_point& t) {
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - t).count();
    t = now;
    return ms;
}

// Applies brush dabs to a mesh, keeping normals and the BVH in sync and
// recording the original positions of every touched vertex for undo.
class Sculptor {
public:
    // Strokes write into sculpt layer `layer` (-1 = base). Returns false if the
    // layer's weight is ~0, since its offsets would be invisible.
    bool beginStroke(const Mesh& mesh, std::string label, int layer = -1);
    void dab(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p, Symmetry sym);
    // Custom brush defined by a recipe (see BrushRecipe.h).
    void dabRecipe(Mesh& mesh, BVH& bvh, const BrushRecipe& recipe, const DabParams& p, Symmetry sym);

    // Move brush: capture the region once, then drag it by world deltas.
    void beginGrab(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym);
    void grab(Mesh& mesh, BVH& bvh, const glm::vec3& delta);

    // Brushes that change mask or polypaint call these before writing
    // mesh.mask[v] / mesh.colors[v] (after ensureMask()/ensureColors()), so
    // the stroke's undo step restores them. Each vertex is recorded once per stroke.
    void recordMask(const Mesh& mesh, uint32_t v);
    void recordColor(const Mesh& mesh, uint32_t v);

    UndoRecord endStroke();
    bool strokeActive() const { return active_; }

    SculptTimings timings;
    // The brush's frame on the surface at the last dab's first (unmirrored) pass: the dab centre, the area normal
    // and the area centre its kernel used (the predictive stroke preview estimates the next dabs from it).
    struct DabFrame {
        glm::vec3 center{0.0f}, normal{0.0f, 0.0f, 1.0f}, areaCenter{0.0f};
        bool valid = false;
    };
    DabFrame lastFrame;

    // Dynamic topology (core/Dyntopo.h): while enabled, dab() and dabRecipe() remesh the surface under the
    // brush first (Smooth towards twice the edge length, so it simplifies, as in ZBrush). Not the Move brush.
    DyntopoSettings dyntopo;
    DynamicTopology& dynamicTopology() { return dynTopo_; }
    // No remeshing for the rest of this stroke: strokes that undo and re-stamp while dragging (DragRect,
    // DragDot; restoreStroke) put positions back, not topology. restoreStroke holds it too.
    void holdTopology() { topologyHeld_ = true; }

    // The cavity term (Mesh::curvatureAt) of every vertex whose normal this stroke's dabs updated, by vertex,
    // worked out with the normals (the renderer packs it instead of walking the rings again); null outside a
    // stroke. Entries of other vertices are stale.
    const float* strokeCurvature() const { return active_ && !curvature_.empty() ? curvature_.data() : nullptr; }

    // Draft strokes (Preferences > Performance > Draft Strokes): when set before beginStroke (it asks once), dabs move the vertices
    // and refit the BVH but leave the normals and the cavity term as they were (the brushes that read normals see
    // the stroke-start ones, so the result differs slightly from a full stroke); the vertices are only marked
    // for the GPU. flushDraft works out every normal and cavity term the stroke touched, once (the app calls
    // it before endStroke). Not with dynamic topology.
    bool draftStrokes = false;
    bool draftActive() const { return draft_; }
    bool draftPending() const { return draft_ && !draftLeaves_.empty(); }
    void flushDraft(Mesh& mesh, const BVH& bvh);

    // Mirror sign vectors for the enabled symmetry axes (always starts with (1,1,1)).
    static std::vector<glm::vec3> mirrors(Symmetry sym);

    // Puts every vertex the current stroke moved (or painted) back the way it
    // started (the stroke stays open). DragRect re-stamps from scratch while dragging.
    void restoreStroke(Mesh& mesh, BVH& bvh);

private:
    void dabSingle(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p);
    void dabRecipeSingle(Mesh& mesh, BVH& bvh, const BrushRecipe& r, const DabParams& p);
    // The Vdm brush (defined in Vdm.cpp); `handedness` -1 for a mirrored pass, so the stamp mirrors exactly.
    void dabVdmSingle(Mesh& mesh, BVH& bvh, const DabParams& p, float handedness);
    void finishDab(Mesh& mesh, BVH& bvh, std::chrono::steady_clock::time_point clock);
    void updateDabNormals(Mesh& mesh, const BVH& bvh);
    void touch(Mesh& mesh, uint32_t v);
    // The one place brushes move vertices: scales `d` by vertexInfluence().
    void displace(Mesh& mesh, uint32_t v, const glm::vec3& d);
    void displaceAll(Mesh& mesh, std::span<const uint32_t> verts, std::span<const glm::vec3> d);

    // Dynamic topology: remeshes before a dab, and keeps the stroke's per-vertex state in step with it.
    void remeshDab(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym, float scale, float reach);
    void repairDab(Mesh& mesh, BVH& bvh);
    void followRemesh(Mesh& mesh);
    void mergeTopologyUndo(TopologyUndo& topo, const Mesh& mesh);
    DynamicTopology dynTopo_;
    float strokeDiagonal_ = 0.0f;
    // The last remesh (repairDab remeshes it again when the brush squashed a triangle).
    bool lastRemesh_ = false;
    bool topologyHeld_ = false;
    glm::vec3 lastCenter_{0.0f};
    float lastRadius_ = 0.0f, lastEdge_ = 0.0f;
    std::vector<glm::vec3> lastMirrors_;

    bool active_ = false;
    bool frameWanted_ = false; // the next dabSingle records lastFrame
    int layer_ = -1;
    UndoRecord record_;
    // A bit per vertex: recorded in record_'s positions / mask / colours (cleared from those lists).
    std::vector<uint64_t> touched_, masked_, colored_;
    uint64_t strokeId_ = 0;
    void clearRecorded();

    std::vector<uint32_t> verts_, dabVerts_; // one mirror's vertices; every mirror's (for the normals)
    std::vector<uint32_t> dabLeaves_;        // the BVH leaves every mirror's query listed (the normals' set)
    std::vector<float> curvature_;           // strokeCurvature
    bool curvatureFilled_ = false;           // every entry current (a draft stroke packs them as they are)
    const Mesh* curvatureMesh_ = nullptr;    // the mesh curvature_ is for
    bool draft_ = false;                     // this stroke is a draft
    std::vector<uint32_t> draftLeaves_;      // the BVH leaves its dabs touched (flushDraft)
    std::vector<glm::vec3> scratch_;
    std::vector<float> weights_, influence_;
    std::vector<uint8_t> firstTouch_;
    std::vector<uint32_t> blockFirst_;
    // Move brush: each mirror's vertices and weights (grabMirrors_: where each mirror's run ends, and its
    // signs), every vertex once (sorted), and the BVH leaves holding them.
    std::vector<uint32_t> grabVerts_;
    std::vector<float> grabWeights_;
    std::vector<std::pair<size_t, glm::vec3>> grabMirrors_;
    std::vector<uint32_t> grabAll_, grabLeaves_;

    // Layer brush: per-stroke height already applied to each vertex.
    void prepareLayer(const Mesh& mesh, float height);
    std::vector<uint32_t> layerStamp_;
    std::vector<float> layerDone_;
    std::vector<glm::vec3> layerDir_;
    uint32_t layerEpoch_ = 0;
    uint64_t layerSeenStroke_ = 0;
    float layerHeight_ = 0.0f;

public:
    // Mask brush (Ctrl in the app; defined in Masking.cpp): raises the mask
    // under the brush towards 1, or lowers it towards 0 when p.invert is set.
    // Ignores the existing mask (it edits it) but skips hidden vertices.
    void dabMask(Mesh& mesh, const BVH& bvh, const DabParams& p, Symmetry sym);
};

} // namespace sf
