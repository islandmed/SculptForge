// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/BVH.h"
#include "core/BrushRecipe.h"
#include "core/Dyntopo.h"
#include "core/Kelvinlet.h"
#include "core/Mesh.h"
#include "core/Symmetry.h"
#include "core/Undo.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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
    // Kelvinlet brushes (core/Kelvinlet.h): physically based elastic deformation.
    KelvinGrab,  // elastic move: drags the surface like clay, bulging with Squash (Poisson's ratio)
    KelvinTwist, // turns the surface around the view axis, fading smoothly
    KelvinScale, // pinches (or swells) the surface toward the centre
    KelvinSnake, // multi-scale elastic SnakeHook: pulls tapering horns out along the stroke
    DetailClone, // stamps the VDM brush's map along the stroke, each stamp turned to follow it (Kelvinlet.cpp)
    Morph,       // pulls the surface toward the Part's morph target, a selection set's shape (core/SelectionSets.h)
    Count
};

// The brushes Kelvinlet.cpp implements (Sculptor::dab hands them over).
inline bool isKelvinletBrush(BrushType t) {
    return t == BrushType::KelvinGrab || t == BrushType::KelvinTwist || t == BrushType::KelvinScale ||
           t == BrushType::KelvinSnake || t == BrushType::DetailClone;
}

// The brush's stored name: the key projects, brush files, hotkeys ("brush.damstandard") and scripts (stroke
// brush=DamStandard) use. Never changes, so old files and scripts keep working.
const char* brushName(BrushType type);
// The name the interface shows ("Sharp Crease" for DamStandard); brushes named after ZBrush's coinages have their own.
const char* brushLabel(BrushType type);
// The brush a name means: its stored name or its label (any case, spaces ignored). False when none.
bool brushFromName(std::string_view name, BrushType& out);

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
    float focal = 0.0f;                  // ZBrush Focal Shift, -1..1 (brushFalloff; alphaFalloff with an alpha)
    const VectorDisplacement* vdm = nullptr; // the Vdm brush's map (null = the built-in Horn); top edge = alphaUp
    // Smooth: keep the volume (#40). A Taubin step, one shrink then one inflate, removes bumps as the plain
    // (Laplacian) smooth does without shrinking the form. Not on micro levels (they smooth plainly).
    bool keepVolume = false;
    // Smooth: face group borders only (#81, clothing edges). Only vertices on a border between face groups move,
    // each toward its two neighbours along the border and kept on the surface's tangent plane, so a jagged border
    // straightens into a clean line while the rest stays put. Junctions of three or more groups hold still.
    bool groupBorders = false;
    // Falloff options (#55, Blender's proportional editing): `connectedOnly` keeps the dab to the surface joined
    // to the point under the cursor (the other leg, the next finger stay put); `viewFalloff` measures the falloff
    // on screen, so the brush reaches through the model along the view as a cylinder.
    bool connectedOnly = false;
    bool viewFalloff = false;
};

// Local symmetry's centre for a Part: the middle of its bounding box (in parallel).
glm::vec3 symmetryPivot(const Mesh& mesh);
// A dab's parameters for one copy: its centre, view direction, stroke delta and alpha orientation moved by it.
inline DabParams transformed(const DabParams& p, const SymCopy& c) {
    DabParams o = p;
    o.center = c.point(p.center);
    o.viewDir = c.dir(p.viewDir);
    o.strokeDelta = c.dir(p.strokeDelta);
    o.alphaUp = c.dir(p.alphaUp);
    return o;
}

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
// Distance from the dab centre for the falloff: in space, or (viewFalloff) across the view only.
inline float brushDistance(const glm::vec3& x, const glm::vec3& center, const glm::vec3& viewDir, bool viewFalloff) {
    const glm::vec3 o = x - center;
    if (!viewFalloff) return glm::length(o);
    const float l2 = glm::dot(viewDir, viewDir);
    return l2 > 1e-24f ? glm::length(o - viewDir * (glm::dot(o, viewDir) / l2)) : glm::length(o);
}
inline float brushDistance(const struct DabParams& p, const glm::vec3& x) {
    return brushDistance(x, p.center, p.viewDir, p.viewFalloff);
}
// Scratch for keepConnected (a stamp per vertex, so nothing is cleared between dabs).
struct ConnectedScratch {
    std::vector<uint32_t> stamp, queue;
    uint32_t epoch = 0;
};
// Connected only: keeps the vertices of `verts` (in increasing order) that the vertex of `verts` nearest `seed`
// reaches through edges between vertices of `verts`; the order stays.
void keepConnected(const Mesh& mesh, const glm::vec3& seed, std::vector<uint32_t>& verts, ConnectedScratch& scratch);
// The vertices a dab may touch with the falloff options: brushVertices, or the cylinder along `viewDir`
// (viewFalloff), then keepConnected from `center` (connectedOnly).
void brushRegion(const Mesh& mesh, const BVH& bvh, const glm::vec3& center, const glm::vec3& viewDir, float radius,
                 bool connectedOnly, bool viewFalloff, std::vector<uint32_t>& out, ConnectedScratch& scratch);
// Along the normal (from the discussions: Move, Tendril, Elastic Tendril and Elastic Grab with Alt): the
// falloff-weighted average normal of the visible surface within `radius` of `center`, unit length (zero when
// there is no surface there).
glm::vec3 brushAreaNormal(const Mesh& mesh, const BVH& bvh, const glm::vec3& center, float radius, float focal = 0.0f);
// The parameter s of the point point + s * dir (dir unit length) nearest the ray origin + t * rayDir: where a
// cursor ray says a grab sliding along the line should be. False when the line is within `minSine` (the sine of
// the angle between them) of parallel to the ray, so the cursor can't say where along it.
bool lineRayClosest(const glm::vec3& point, const glm::vec3& dir, const glm::vec3& origin, const glm::vec3& rayDir,
                    float& s, float minSine = 0.25f);
// How far one Clay / Clay Block dab pulls the surface up to its plane (#83), from the dab's strength (Intensity x
// pressure, 0..1): all the way from the default Intensity (50) up, less below it, so a light touch or a low
// Intensity builds gently instead of planing the surface flat as a full-strength dab would.
// Face group borders (#81): the neighbours of `v` across whose shared edge the face group changes, up to 3
// (`out`); the return value is how many there are (a count of 3 means a junction).
int groupBorderNeighbours(const Mesh& mesh, uint32_t v, uint32_t out[3]);
inline float clayPull(float s) { return s >= 0.5f ? 1.0f : 2.0f * std::max(s, 0.0f); }
// Smoothstep brush falloff: 1 at the centre, 0 at `radius`. `focal` is ZBrush's Focal Shift (-1..1): below 0
// the full-strength centre widens (a harder, broader tip), above 0 it narrows to a softer point; 0 is the plain
// smoothstep, unchanged.
float brushFalloff(float dist, float radius, float focal = 0.0f);
// Where brushFalloff drops to one half, as a fraction of the radius (the brush cursor's inner ring).
float focalRingFraction(float focal);
// An alpha's stamp covers the square around the brush circle: its corners are radius x sqrt 2 out.
constexpr float kAlphaReach = 1.4143f;
// Focal Shift on an alpha brush (#60), as in ZBrush: the alpha is multiplied by this round falloff, which reaches
// the stamp's corners (radius x kAlphaReach). -1 is 1 everywhere (the alpha shows hard to its edges), 0 the
// smoothstep (soft, rounding a square alpha's corners), +1 a narrow soft point (the alpha mostly gone).
float alphaFalloff(float dist, float radius, float focal);

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
    void grab(Mesh& mesh, BVH& bvh, const glm::vec3& delta); // grabMove, then grabFinish
    // The two halves: the positions (each pointer sample), then the normals and the BVH of what moved (once a
    // frame: they depend only on where the vertices ended up, so the mesh is the same, bit for bit).
    void grabMove(Mesh& mesh, const glm::vec3& delta);
    void grabFinish(Mesh& mesh, BVH& bvh);

    // Brushes that change mask or polypaint call these before writing
    // mesh.mask[v] / mesh.colors[v] (after ensureMask()/ensureColors()), so
    // the stroke's undo step restores them. Each vertex is recorded once per stroke.
    void recordMask(const Mesh& mesh, uint32_t v);
    void recordColor(const Mesh& mesh, uint32_t v);

    UndoRecord endStroke();
    bool strokeActive() const { return active_; }
    // Whole-mesh operations (core/Deform.h) inside a stroke: moves `verts` (each once, increasing) by `d` as given
    // (the caller weighed in the mask), recorded for undo and written to the stroke's layer; then their normals
    // and the BVH.
    void moveVertices(Mesh& mesh, BVH& bvh, std::span<const uint32_t> verts, std::span<const glm::vec3> d);

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

    // Kelvinlet brushes (core/Kelvinlet.h, defined in Kelvinlet.cpp): one step of KelvinGrab / KelvinSnake
    // (step.delta), KelvinTwist (step.angle) or KelvinScale (step.logScale), mirrored by `sym`. dab() turns a plain
    // dab into steps. The settings are Brush > Kelvinlet's.
    KelvinletSettings kelvinlet;
    void kelvinletStep(Mesh& mesh, BVH& bvh, BrushType type, const KelvinletStep& step, Symmetry sym);
    // Detail Clone: a stroke that never moved has its one stamp waiting (for a direction): this presses it.
    // Call before endStroke() (harmless for every other brush).
    void flushDetailClone(Mesh& mesh, BVH& bvh, Symmetry sym);
    // A dense Kelvinlet stroke defers part of its far field (Kelvinlet.cpp, KelvinFarField): whether some is still
    // waiting, and applying it (one group of vertices, or all; flushDetailClone applies all at a stroke's end).
    bool kelvinletFarPending() const;
    void flushKelvinletFar(Mesh& mesh, BVH& bvh, bool all);
    // The last kelvinletStep found more vertices than kelvinlet.vertexBudget (the app then moves less per frame).
    bool kelvinletOverBudget() const { return kelvinOverBudget_; }

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

    // Micro level timings (findMs, kernelMs, stitchMs, points) of the current stroke (core/Micro.h).
    const MicroStroke& microStroke() const { return micro_; }
    // Move on a micro level: false (default) deforms the base cage and the detail rides along; true moves the
    // micro displacement along its normals instead.
    bool microMoveAlongNormals = false;
    MicroStroke& microStroke() { return micro_; }
    // Mirror sign vectors for the enabled symmetry axes (always starts with (1,1,1)); ignores radial and local.
    static std::vector<glm::vec3> mirrors(Symmetry sym);
    // Every copy of a stroke (core/Symmetry.h, symmetryCopies), the identity first.
    static std::vector<SymCopy> copies(Symmetry sym) { return symmetryCopies(sym); }

    // Puts every vertex the current stroke moved (or painted) back the way it
    // started (the stroke stays open). DragRect re-stamps from scratch while dragging.
    void restoreStroke(Mesh& mesh, BVH& bvh);

    // Morph brush (core/SelectionSets.h): the shape it pulls toward at the mesh's current level, from the selection
    // set the Part names as its morph target. Null when there is none or it doesn't fit the mesh. Worked out on
    // first use and kept while the set, level and vertex count stay the same (defined in SelectionSets.cpp).
    const std::vector<glm::vec3>* morphTarget(const Mesh& mesh);

private:
    std::shared_ptr<const struct SelectionSet> morphSet_;
    int morphLevel_ = -1;
    size_t morphVertices_ = 0;
    std::vector<glm::vec3> morphMapped_;
    bool morphFits_ = false;
    void dabSingle(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p);
    void dabRecipeSingle(Mesh& mesh, BVH& bvh, const BrushRecipe& r, const DabParams& p);
    // The Vdm brush (defined in Vdm.cpp); `handedness` -1 for a mirrored pass, so the stamp mirrors exactly.
    void dabVdmSingle(Mesh& mesh, BVH& bvh, const DabParams& p, float handedness);
    // Kelvinlet.cpp: the Kelvinlet brushes and Detail Clone.
    void dabKelvinlet(Mesh& mesh, BVH& bvh, BrushType type, const DabParams& p, Symmetry sym);
    void kelvinletSingle(Mesh& mesh, BVH& bvh, const KelvinletKernel& k, const KelvinletStep& s);
    void dabDetailClone(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym);
    void cloneStamp(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym);
    struct CloneState {
        uint64_t stroke = 0;
        bool pending = false, stamped = false;
        DabParams first;
        glm::vec3 last{0.0f};
    } clone_;
    bool kelvinRingNormals_ = false; // a Kelvinlet pass moved only part of what it queried
    std::shared_ptr<struct KelvinFarField> kelvinFar_;
    void gatherKelvinletFar(const std::function<glm::vec3(const glm::vec3&)>& displacement, const glm::vec3& a,
                            const glm::vec3& ab, float radius, const glm::vec3& mid, float reach);
    void applyKelvinletFarGroup(Mesh& mesh, BVH& bvh, int group);
    bool kelvinOverBudget_ = false;  // the last kelvinletStep's region held more than kelvinlet.vertexBudget vertices
    uint64_t kelvinStroke_ = 0;
    size_t kelvinLastVerts_ = 0;     // the last move's region: its vertex count and reach (the next one's prediction)
    float kelvinLastCut_ = 0.0f;
    uint64_t snakeStroke_ = 0;
    float snakeTravel_ = 0.0f;
    void finishDab(Mesh& mesh, BVH& bvh, std::chrono::steady_clock::time_point clock);
    void updateDabNormals(Mesh& mesh, const BVH& bvh);
    void touch(Mesh& mesh, uint32_t v);
    // The one place brushes move vertices: scales `d` by vertexInfluence().
    void displace(Mesh& mesh, uint32_t v, const glm::vec3& d);
    void displaceAll(Mesh& mesh, std::span<const uint32_t> verts, std::span<const glm::vec3> d, bool influence = true);

    // Dynamic topology: remeshes before a dab, and keeps the stroke's per-vertex state in step with it.
    void remeshDab(Mesh& mesh, BVH& bvh, const DabParams& p, Symmetry sym, float scale, float reach);
    void repairDab(Mesh& mesh, BVH& bvh);
    void followRemesh(Mesh& mesh);
    void mergeTopologyUndo(TopologyUndo& topo, const Mesh& mesh);
    DynamicTopology dynTopo_;
    // Micro-displacement levels (core/Micro.h): strokes on a mesh with one go to its grid instead.
    MicroStroke micro_;
    Mesh* microMesh_ = nullptr;
    bool microReady(Mesh& mesh); // the mesh has a micro level: starts the micro stroke on first use
    float strokeDiagonal_ = 0.0f;
    // The last remesh (repairDab remeshes it again when the brush squashed a triangle).
    bool lastRemesh_ = false;
    bool topologyHeld_ = false;
    glm::vec3 lastCenter_{0.0f};
    float lastRadius_ = 0.0f, lastEdge_ = 0.0f;
    std::vector<SymCopy> lastMirrors_;

    bool active_ = false;
    bool frameWanted_ = false; // the next dabSingle records lastFrame
    int layer_ = -1;
    UndoRecord record_;
    // A bit per vertex: recorded in record_'s positions / mask / colours (cleared from those lists).
    std::vector<uint64_t> touched_, masked_, colored_;
    uint64_t strokeId_ = 0;
    void clearRecorded();

    std::vector<uint32_t> verts_, dabVerts_; // one mirror's vertices; every mirror's (for the normals)
    ConnectedScratch connected_;             // Connected only (DabParams::connectedOnly)
    std::vector<uint32_t> dabLeaves_;        // the BVH leaves every mirror's query listed (the normals' set)
    std::vector<float> curvature_;           // strokeCurvature
    bool curvatureFilled_ = false;           // every entry current (a draft stroke packs them as they are)
    const Mesh* curvatureMesh_ = nullptr;    // the mesh curvature_ is for
    bool draft_ = false;                     // this stroke is a draft
    std::vector<uint32_t> draftLeaves_;      // the BVH leaves its dabs touched (flushDraft)
    std::vector<glm::vec3> scratch_;
    // Smooth with keepVolume: each brush vertex's shrunk position, and (vertex, index) sorted to look it up.
    void taubinShrink(const Mesh& mesh);
    std::vector<glm::vec3> shrunk_;
    std::vector<std::pair<uint32_t, uint32_t>> shrunkIndex_;
    std::vector<float> weights_, influence_;
    std::vector<uint8_t> firstTouch_;
    std::vector<uint32_t> blockFirst_;
    // Move brush: each mirror's vertices and weights (grabMirrors_: where each mirror's run ends, and its
    // matrix), every vertex once (sorted), and the BVH leaves holding them.
    std::vector<uint32_t> grabVerts_;
    std::vector<float> grabWeights_;
    std::vector<std::pair<size_t, glm::mat3>> grabMirrors_;
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
