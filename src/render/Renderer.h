// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Matcap.h"
#include "core/Mesh.h"
#include "core/Splats.h"
#include "core/UploadOrder.h"
#include "render/Camera.h"
#include "render/Clusters.h"
#include "render/GL.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sf {

struct RenderSettings {
    bool wireframe = false;
    glm::vec3 baseColor{0.80f, 0.55f, 0.45f}; // clay
    bool showPolypaint = true;   // vertex colours replace baseColor when the mesh has them
    bool showTexture = true;     // with showPolypaint: a SubTool's painted texture (core/Texture.h) multiplies in
    bool showMask = true;        // masked areas render darker (ZBrush style)
    bool showPolygroups = false; // colour each polygroup (see polygroupColor)
    int matcap = defaultMatcap(); // built-in MatCap material (core/Matcap.h), Soft Clay; -1 = studio clay lighting
    bool showMesh = true;
    bool showSplats = true;
    float splatOpacity = 1.0f; // global fade for using a capture as reference
    bool dimInactive = true;   // SubTools other than the active one render slightly darker (ZBrush style)
    bool motionProxy = false;  // the view is moving (Fast Navigation): draw big meshes' lighter proxy (render/Clusters.h)
};

// A baked tangent-space normal map to preview on a mesh (core/Bake.h): RGB8 rows bottom-up, with the
// MikkTSpace tangent + sign of every triangle corner it was baked against. `serial` changes with the data.
struct NormalMapView {
    uint64_t serial = 0;
    int width = 0, height = 0;
    const std::vector<uint8_t>* rgb = nullptr;
    const std::vector<glm::vec4>* tangents = nullptr;
    bool flipGreen = false; // DirectX convention (green down)
};

// One mesh of the scene for Renderer::syncScene: a SubTool, keyed by its stable id.
struct SceneMesh {
    uint32_t id = 0;
    Mesh* mesh = nullptr;
    bool active = false; // gets mask darkening, polygroup colours and wireframe; never dimmed
    bool visible = true; // hidden meshes keep their GPU slot but aren't drawn (or uploaded)
    const NormalMapView* normalMap = nullptr; // shade with this normal map (needs the mesh's UVs)
    bool wireframe = false; // keep the wireframe's edges on the GPU (built on first need; draw() falls back to
                            // outlining the triangles, quad diagonals included, until they are)
};

// Part of the target the 3D view is drawn into (framebuffer pixels, origin bottom-left); the rest is
// cleared to `surround`. w == 0: the whole target.
struct RenderViewport {
    int x = 0, y = 0, w = 0, h = 0;
    glm::vec3 surround{0.118f, 0.118f, 0.125f};
};

// Display colour of a polygroup id (linear RGB): a stable hash spread over
// pleasant hues. The mesh shader uses the same formula.
glm::vec3 polygroupColor(uint32_t id);

// Draws every visible SubTool, each from its own GPU buffers (a slot keyed by
// SubTool id), in clusters (core/UploadOrder.h) culled on the CPU every frame
// (render/Clusters.h) and drawn with one multi-draw. Mesh drawing honours the optional Mesh attributes: colours
// (polypaint), mask (darkening), polygroups (group colours) and hidden
// triangles (not drawn). Mark changed vertices dirty for colour/mask edits and
// set Mesh::facesDirty after changing `hidden` or `polygroups`. Only what a
// mesh marks dirty is uploaded, so inactive SubTools upload once.
class Renderer {
public:
    bool init(std::string& error);
    void shutdown();
    // Makes `scene` what draw() shows, in order: uploads whatever each visible
    // mesh marked dirty and clears its dirty state (a new slot uploads
    // everything), and frees the slots of ids no longer listed.
    void syncScene(const std::vector<SceneMesh>& scene);
    // True when SubTool `id` didn't fit in GPU memory at its last full upload (it isn't drawn until it does).
    bool outOfMemory(uint32_t id) const;
    // Re-uploads the splats when `version` differs from the last upload.
    void syncSplats(const SplatCloud& splats, uint64_t version);
    using Viewport = RenderViewport;
    void draw(const Camera& camera, int fbWidth, int fbHeight, const RenderSettings& settings, const Viewport& viewport = {});
    // draw() for the app's window (whose framebuffer has no multisampling): through an offscreen target with
    // `samples` samples, resolved to the screen, or straight to the screen for 1. When the GPU can't make the
    // target (e.g. out of memory) it uses fewer samples. Returns the samples used.
    int drawView(const Camera& camera, int fbWidth, int fbHeight, const RenderSettings& settings, const Viewport& viewport,
                 int samples);
    // Offscreen render (4x MSAA) into top-down RGBA8 pixels.
    bool renderToImage(const Camera& camera, int width, int height, const RenderSettings& settings,
                       std::vector<uint8_t>& rgba, std::string& error);

    // Persistent offscreen target (4x MSAA) for recording: draw between these calls.
    bool beginCapture(int width, int height, std::string& error);
    void endCapture(std::vector<uint8_t>& rgba); // top-down RGBA rows

    // GL texture of the painted texture of SubTool `id` as last synced (0 if none), for UI previews (rows bottom-up).
    GLuint paintTexture(uint32_t id) const;

    // Bytes of GPU buffers and textures the mesh slots hold.
    size_t gpuBytes() const;
    // Triangles the last draw() sent to the GPU (after culling), for the benchmark.
    size_t lastDrawnTriangles() const { return lastDrawn_; }

    // Texture of built-in matcap `index` (sRGB RGBA8, rows bottom-up) for UI previews; 0 if none.
    GLuint matcapTexture(int index) const;

    // --- Thumbnails (LightBox / pickers) ---
    // Renders `mesh` alone (not the synced scene, which is left as it was) into a `size` x `size`
    // top-down RGBA8 image with a transparent background. Keeps the caller's framebuffer and viewport.
    bool renderMeshThumbnail(Mesh& mesh, const Camera& camera, int size, const RenderSettings& settings,
                             std::vector<uint8_t>& rgba, std::string& error);

private:
    void drawSplats(const glm::mat4& view, const glm::mat4& proj, int fbWidth, int fbHeight, float opacity);

    GLuint wireProgram_ = 0, bgProgram_ = 0, splatProgram_ = 0;
    // Splats: per-splat data in a texture buffer, drawn as instanced quads in back-to-front order.
    GLuint splatVao_ = 0, splatData_ = 0, splatTexture_ = 0, splatOrder_ = 0;
    GLsizei splatCount_ = 0;
    uint64_t splatVersion_ = ~uint64_t(0);
    std::vector<glm::vec3> splatCenters_;
    std::vector<uint32_t> splatSorted_;
    glm::mat4 lastSortView_{0.0f};
    GLuint captureFbo_[2] = {}, captureRb_[3] = {};
    // drawView's multisampled target; viewFailed_ = the sample count that couldn't be made at this size.
    bool viewTarget(int width, int height, int samples);
    GLuint viewFbo_ = 0, viewRb_[2] = {};
    int viewW_ = 0, viewH_ = 0, viewSamples_ = 0, viewFailed_ = 0;
    int captureW_ = 0, captureH_ = 0;
    GLuint emptyVao_ = 0;
    bool thumbnailPass_ = false; // draw(): transparent background, no gradient (renderMeshThumbnail)
    // The GPU copy of one mesh (one SubTool), in the cluster layout of `order`.
    struct MeshSlot {
        uint32_t id = 0;
        GLuint vao = 0, lineVao = 0;
        GLuint vertexBuffer = 0; // PackedVertex per GPU slot (render/Clusters.h)
        GLuint indexBuffer = 0;  // 16-bit, relative to each cluster's first vertex; kClusterTriangles * 3 per cluster
        GLuint lineBuffer = 0;   // wireframe edges, 16-bit like the triangles, each edge once
        GLuint colorBuffer = 0, maskBuffer = 0; // RGBA8 (gamma encoded), R8
        GLuint groupData = 0, groupTexture = 0; // polygroup id per drawn triangle (texture buffer, R16UI or R32UI)
        GLuint primData = 0, primTexture = 0;   // per GPU block: the first drawn triangle of its cluster (R32UI)
        GLuint uvData = 0, uvTexture = 0; // per drawn triangle corner UVs (texture buffer), meshes with a texture
        GLuint paintTexture = 0;          // the painted texture (sRGB, mipmapped)
        uint64_t textureSerial = 0, textureRevision = 0;
        int textureW = 0, textureH = 0;
        // Baked normal map preview (NormalMapView): the map, and the tangents per drawn corner (texture buffer).
        GLuint normalTexture = 0, tangentData = 0, tangentTexture = 0;
        uint64_t normalSerial = 0;
        bool hasTangents = false, tangentsStale = true, normalFlipGreen = false;
        bool hasColors = false, hasMask = false, hasGroups = false, hasUvs = false;
        bool fresh = true; // nothing uploaded yet
        bool outOfMemory = false; // the last full upload ran out of GPU memory: not drawn
        UploadOrder order; // the cluster layout
        PositionGrid grid; // the positions' quantisation
        std::vector<ClusterBounds> bounds; // per cluster, for culling
        AABB box;                          // all clusters
        double volume = 0.0;               // sum of the clusters' volumes (> 0: wound outwards)
        bool closed = false;               // closedOriented: back-facing clusters may be culled (without hidden triangles)
        bool anyHidden = false;
        std::vector<GLsizei> drawCount;    // per cluster: indices drawn (hidden triangles left out)
        std::vector<uint32_t> primBase;    // per cluster: its first drawn triangle
        std::vector<uint32_t> lineFirst, lineCount; // per cluster: its edges in lineBuffer (in indices)
        bool linesValid = false;
        // Motion proxy (clusterProxy): per cluster up to a quarter of its triangles, at (firstTriangle / 4) * 3.
        GLuint proxyVao = 0, proxyBuffer = 0;
        std::vector<uint32_t> proxyCount;    // indices; 0: none (the cluster draws in full)
        std::vector<uint8_t> proxyStale;     // to rebuild before use
        std::vector<ClusterBounds> proxyBounds; // the proxy's normal cone
        bool proxyAllocated = false;
        std::vector<uint32_t> drawnOrder; // the mesh triangle of each drawn triangle (meshes with UVs only)
        size_t drawnTriangles = 0;
        size_t vertexCapacity = 0;
        std::vector<uint64_t> blockHash; // per UploadOrder block: hashes of its packed positions, and the rest
        size_t bytes[8] = {}; // GPU sizes: vertices, indices, lines, colours, mask, groups, prims, uvs + tangents

        // Dynamic topology (Mesh::changedTriangles, core/Dyntopo.h): the clusters whose triangles changed since
        // the layout was built, and blocks of triangles added past its end, are drawn from patches instead (the
        // layout skips them): each holds a cluster's current triangles with copies of their vertices, in a fixed
        // part of the patch buffers. A full upload starts over without patches.
        struct Patch {
            uint32_t key = 0;  // layout cluster, or kTailPatch + block of triangles past layoutTriangles
            uint32_t part = 0; // its part of the patch buffers
            uint32_t indices = 0, lines = 0; // drawn
            std::vector<uint32_t> verts;     // the mesh vertex of each local index
            std::vector<uint16_t> local;     // triangles (local indices), while stale
            std::vector<uint32_t> groups;    // their polygroups, while stale
            ClusterBounds bounds;
            bool stale = true;    // triangles changed: rebuild and upload everything
            bool uploaded = false;
        };
        std::vector<Patch> patches;
        std::vector<uint32_t> clusterPatch, tailPatch; // patch per layout cluster / per block past the layout (kNone)
        std::vector<uint32_t> chunkCluster;           // per layout chunk of triangles: its cluster
        uint32_t layoutTriangles = 0;
        GLuint patchVao = 0, patchLineVao = 0, patchVertices = 0, patchIndices = 0, patchLines = 0, patchColors = 0,
               patchMask = 0, patchGroupData = 0, patchGroupTexture = 0;
        size_t patchParts = 0; // parts the patch buffers have room for
        bool patchColorsOn = false, patchMaskOn = false, patchLinesOn = false;
        PositionGrid patchGrid; // the grid the patches' vertices were packed with
        size_t patchBytes = 0;
    };
    MeshSlot& slot(uint32_t id); // created on first use
    static void freeSlot(MeshSlot& s);
    void sync(MeshSlot& s, Mesh& mesh, const NormalMapView* normalMap = nullptr, bool wireframe = false);
    void uploadVertices(MeshSlot& s, const Mesh& mesh, bool full);
    // `computed`: the bounds are already current (only the totals are left).
    void updateBounds(MeshSlot& s, const Mesh& mesh, bool all, bool computed = false);
    void buildLines(MeshSlot& s, const Mesh& mesh);
    void buildProxies(MeshSlot& s, const Mesh& mesh);
    bool proxyUsed_ = false; // the last draw() drew motion proxies: sync keeps them current
    void syncNormalMap(MeshSlot& s, const Mesh& mesh, const NormalMapView* normalMap);
    // `indicesDone`: the index buffer and drawCount are already written (a full upload writes them while
    // building the layout).
    void uploadFaces(MeshSlot& s, const Mesh& mesh, bool wantUvs, bool indicesDone = false);
    void uploadIndices(MeshSlot& s, const Mesh& mesh);
    // Dynamic topology patches: notePatches marks what Mesh::changedTriangles touched, preparePatches rebuilds
    // the stale ones on the CPU (each false when a full upload is due instead: too many, or a patch too big),
    // uploadPatches sends them and every patch vertex the mesh marked dirty, drawPatches draws them.
    bool notePatches(MeshSlot& s, const Mesh& mesh);
    bool preparePatches(MeshSlot& s, const Mesh& mesh, bool wireframe);
    void uploadPatches(MeshSlot& s, const Mesh& mesh, bool wireframe);
    static void resetPatches(MeshSlot& s);
    std::vector<uint32_t> patchLocal_, patchStamp_; // preparePatches: mesh vertex -> local index (this stamp)
    uint32_t patchStampValue_ = 0;
    std::vector<std::pair<uint32_t, uint32_t>> dirtyRuns_; // scratch for sync: UploadOrder::dirtyRuns
    std::vector<uint8_t> staging_;                         // scratch for sync: gathered vertex data
    std::vector<uint8_t> blockChanged_, blockMoved_;      // scratch for uploadVertices, per block
    std::vector<std::pair<uint32_t, uint32_t>> movedRuns_; // the dirty runs whose positions moved
    void syncTexture(MeshSlot& s, Mesh& mesh);
    // The clusters of `s` to draw this frame: in full (drawClusters_), and with `proxy` as their motion proxy
    // where it is built (proxyClusters_).
    void cull(const MeshSlot& s, const CullView& view, bool proxy = false);
    void drawSlot(const MeshSlot& s, bool active, const RenderSettings& settings);
    enum class Part { Triangles, Lines, Proxy };
    void drawPatches(const MeshSlot& s, GLenum mode, Part part, GLint primBaseLocation);
    void drawClusters(const MeshSlot& s, const std::vector<uint32_t>& clusters, GLenum mode, Part part, GLint primBaseLocation);
    std::vector<uint32_t> proxyClusters_;
    std::vector<GLsizei> drawCounts_;
    std::vector<const void*> drawOffsets_;
    std::vector<GLint> drawBases_;
    std::vector<uint32_t> drawClusters_;
    bool cullBackfaces_ = false; // the last cull(): back faces can't show (so the GPU may skip them too)
    std::vector<uint32_t> cullScratch_;
    size_t lastDrawn_ = 0;
    // The frame draw() is drawing: matrices, the view's rectangle (x, y, w, h) and its culling.
    glm::mat4 frameView_{1.0f}, frameProj_{1.0f};
    glm::vec4 frameViewport_{0.0f};
    CullView frameCull_;
    // Mesh shader variants by feature bits (kPaint, kPrim, kTextured, kNormalMap), compiled on first use.
    struct MeshProgram {
        GLuint id = 0;
        GLint view = -1, proj = -1, gridOrigin = -1, gridStep = -1, viewport = -1, projScale = -1, baseColor = -1, paint = -1,
              showMask = -1, groups = -1, matcap = -1, matcapCavity = -1, dim = -1, primBase = -1, hasPaint = -1,
              normalFlipY = -1, twoSided = -1;
    };
    static constexpr int kPaint = 1, kPrim = 2, kTextured = 4, kNormalMap = 8;
    MeshProgram programs_[16];
    std::string programError_;
    const MeshProgram* meshProgram(int features);
    bool vertexIdHasBase_ = true; // gl_VertexID includes the base vertex (checked at init; else per-cluster draws)
    bool checkVertexIdBase();
    std::vector<MeshSlot> slots_;
    struct DrawItem {
        uint32_t id;
        bool active;
    };
    std::vector<DrawItem> drawList_;      // visible meshes, in scene order
    std::vector<uint32_t> visibleGroups_; // scratch for uploadFaces
    std::vector<glm::vec2> visibleUvs_;   // scratch for uploadFaces
    std::vector<GLuint> matcapTextures_; // one per built-in matcap (core/Matcap.h)
};

} // namespace sf
