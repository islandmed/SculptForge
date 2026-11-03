// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/EnvironmentMath.h"
#include "core/Matcap.h"
#include "core/Mesh.h"
#include "core/Splats.h"
#include "core/UploadOrder.h"
#include "render/Camera.h"
#include "render/Clusters.h"
#include "render/GL.h"
#include "render/Ghost.h"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace sf {

class MicroRenderer;

struct RenderSettings {
    bool wireframe = false;
    glm::vec3 baseColor{0.62f, 0.58f, 0.53f}; // Studio's colour: a warm clay grey, like the default Soft Clay
    bool showPolypaint = true;   // vertex colours replace baseColor when the mesh has them
    // The active colour tints the Lit Sphere material on meshes with no vertex paint (Color palette, as ZBrush's
    // main colour does): materialTint multiplies the material there, white leaves it as it is (materialTintFor).
    bool colorTintsMaterial = true;
    glm::vec3 materialTint{1.0f};
    bool showTexture = true;     // with showPolypaint: a SubTool's painted texture (core/Texture.h) multiplies in
    bool showMask = true;        // masked areas render darker (ZBrush style)
    bool showPolygroups = false; // colour each polygroup (see polygroupColor)
    int matcap = defaultMatcap(); // built-in MatCap material (core/Matcap.h), Soft Clay; -1 = studio clay lighting
    bool showMesh = true;
    bool showSplats = true;
    float splatOpacity = 1.0f; // global fade for using a capture as reference
    bool dimInactive = true;   // SubTools other than the active one render slightly darker (ZBrush style)
    bool motionProxy = false;  // the view is moving (Fast Navigation): draw big meshes' lighter proxy (render/Clusters.h)
    bool flatShading = false;  // the scene's shading (Material palette): faceted; SubTools can override (core/Document.h)
    // Backface culling (#68, Tool > Display): back faces are never drawn, on any mesh (closed or not, from any
    // view). Off: they are skipped only where they can't show (a closed mesh seen from outside, Renderer::cull).
    bool backfaceCulling = false;
    // The view's background: the charcoal radial gradient (default) or one flat display (sRGB) colour (#21).
    bool gradientBackground = true;
    glm::vec3 backgroundColor{0.168f, 0.168f, 0.168f};
    // The gradient's colours (display sRGB): behind the model and at the edges. The interface look sets them
    // (Preferences > Interface > Look); these are the Classic look's charcoal.
    glm::vec3 gradientCentre{0.215f, 0.215f, 0.222f};
    glm::vec3 gradientEdge{0.106f, 0.106f, 0.114f};
    // Micro-displacement levels (render/MicroRenderer.h): the most triangles a frame draws for them (their level
    // of detail drops to fit; a hardware profile may set it), and the micro edge length to aim for on screen.
    size_t renderTriangleBudget = 0; // 0 = TierConfig::activeTriangleBudget (core/TierConfig.h)
    float microTargetPx = 2.0f;
    // Viewport shading: Lit Sphere (the matcap or studio lights above) or PBR, which shades the material channels
    // (core/TextureChannels.h) with image-based lighting from an environment (core/EnvironmentMath.h): GGX
    // specular through a prefiltered environment and a BRDF table, diffuse from its SH9, exposure and a tone map.
    // Meshes without a PBR texture use pbrRoughness / pbrMetallic with their colour.
    enum class Shading : uint8_t { LitSphere, Pbr };
    Shading shading = Shading::LitSphere;
    int environment = 0;              // index into environments()
    float environmentRotation = 0.0f; // degrees about the up axis
    float exposure = 0.0f;            // stops
    ToneMap toneMap = ToneMap::Aces;
    bool environmentBackground = false; // show the environment behind the model (blurred by backgroundBlur)
    float backgroundBlur = 0.4f;        // 0 = sharp .. 1 = the roughest level
    float pbrRoughness = 0.45f, pbrMetallic = 0.0f;
    float pbrCavity = 0.5f; // how much the sculpt's creases darken under PBR (0 = physically plain)
    bool pbr() const { return shading == Shading::Pbr; }
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

// What the UV Editor shows on the model (app/PanelUvEditor.cpp): a colour per triangle (stretch, texel density,
// islands, the selection) and / or a checker on the UVs. `serial` changes whenever the colours or the checker do.
struct MeshOverlay {
    uint64_t serial = 0;
    std::vector<uint32_t> colors; // per triangle, 0xBBGGRR (sRGB); empty = the usual colours
    bool checker = false;         // a UDIM-tinted checker on the UVs instead of the base colour
    float checkerScale = 8.0f;    // checker squares per UV unit
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
    bool flat = false;      // flat shading: each triangle shows its own face normal (faceted)
    bool pbr = false;       // the view shades PBR: upload the texture's material channels and their tangents
    const MeshOverlay* overlay = nullptr; // the UV Editor's view of the active mesh (must outlive the frame)
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

    // PBR view: an environment's lighting is being built (the view shows the previous one meanwhile). `wake` is
    // called from the worker thread when it is ready (the app redraws then).
    bool environmentPending() const;
    // The PBR planes SubTool `id` has on the GPU (pbrViewPlanes bits: 1 ORM, 2 normal, 4 emissive), for tests.
    uint32_t pbrPlanes(uint32_t id) const;
    void setEnvironmentWake(std::function<void()> wake) { envWake_ = std::move(wake); }

    // GL texture of the painted texture of SubTool `id` as last synced (0 if none), for UI previews (rows bottom-up).
    GLuint paintTexture(uint32_t id) const;

    // --- Strokes on heavy meshes (app/AsyncStroke.cpp) ---
    // The predictive stroke preview (render/Ghost.h) drawn on SubTool `id`; an empty stroke turns it off.
    void setGhost(uint32_t id, const GhostStroke& ghost);
    // A sculpt stroke's vertex upload made in two halves: prepareStrokeUpload packs what the mesh marked dirty
    // (positions and normals only) and the culling bounds of the clusters that moved, on any thread and without
    // GL, while the GL thread keeps drawing from what it has; commitStrokeUpload sends it on the GL thread.
    // prepare clears the mesh's dirty state; it returns false, and leaves the mesh as it was, when the change
    // needs the regular syncScene instead (a new layout, attributes, dynamic topology, a vertex off the grid).
    // Only one prepare at a time, and no syncScene of that mesh until its commit.
    struct StrokeUpload {
        uint32_t id = 0;
        void* slot = nullptr; // bindStrokeUpload
        bool ready = false;
        std::vector<PackedVertex> data;
        struct Range {
            uint32_t begin, end; // GPU slots
            size_t offset;       // into data
        };
        std::vector<Range> ranges;
        std::vector<uint32_t> movedClusters;
        std::vector<ClusterBounds> bounds; // of movedClusters
        std::vector<uint8_t> blockChanged, blockMoved; // scratch, per block
        double packMs = 0.0, boundsMs = 0.0;           // where prepare's time went
        bool regrid = false;                           // every vertex, packed on a new grid
        PositionGrid grid;
        bool exact = false;                            // refineStrokeBoundsInto: exact bounds to set
        std::vector<uint32_t> exactClusters;
        std::vector<ClusterBounds> exactBounds;
        size_t bytes() const { return data.size() * sizeof(PackedVertex); }
    };
    // `curvature` (optional, by vertex): the cavity term of every vertex the mesh marked dirty, already worked
    // out (Sculptor::strokeCurvature).
    bool prepareStrokeUpload(uint32_t id, Mesh& mesh, StrokeUpload& out, const float* curvature = nullptr);
    // GL thread, before handing `up` to another thread: finds SubTool id's slot (slots come and go on the GL
    // thread, e.g. thumbnails; the other thread uses this one, which stays put in the list).
    void bindStrokeUpload(uint32_t id, StrokeUpload& up);
    void commitStrokeUpload(StrokeUpload& up);
    // The stroke uploads gave the clusters they moved quick culling bounds (around their vertices' boxes, which
    // packing finds anyway, without the normal cone, so they are never culled as facing away); at the stroke's
    // end this works out the exact ones (GL thread, the mesh no longer changing).
    void refineStrokeBounds(uint32_t id, const Mesh& mesh);
    // A draft stroke on SubTool `id` (0: none): the clusters its uploads moved draw with per-pixel face normals
    // (theirs are stale) until refineStrokeBounds.
    void setDraftNormals(uint32_t id);
    // refineStrokeBounds on the solver thread at a stroke's end, after prepareStrokeUpload: the exact bounds go
    // into `up` and commitStrokeUpload sets them.
    void refineStrokeBoundsInto(uint32_t id, const Mesh& mesh, StrokeUpload& up);

    // Bytes of GPU buffers and textures the mesh slots hold.
    size_t gpuBytes() const;
    // Triangles the last draw() sent to the GPU (after culling), for the benchmark.
    size_t lastDrawnTriangles() const { return lastDrawn_; }
    // The last syncScene left motion proxies to build (a few milliseconds of them a frame; the rest draw in full).
    bool proxiesPending() const { return proxyPending_; }

    // Texture of matcap `index` (sRGB RGBA8, rows bottom-up) for UI previews; 0 if none (or a custom one not
    // uploaded yet: the next syncScene or draw does).
    GLuint matcapTexture(int index) const;

    // --- Thumbnails (LightBox / pickers) ---
    // Renders `mesh` alone (not the synced scene, which is left as it was) into a `size` x `size`
    // top-down RGBA8 image with a transparent background. Keeps the caller's framebuffer and viewport.
    bool renderMeshThumbnail(Mesh& mesh, const Camera& camera, int size, const RenderSettings& settings,
                             std::vector<uint8_t>& rgba, std::string& error);
    // SubTool thumbnails (#43): SubTool `id` alone as last synced, from the GPU buffers it already has (nothing
    // uploaded, so it stays cheap on 30M+ meshes: those draw their motion proxy where built), framed from the
    // angle `yaw`, `pitch` (degrees) into a `size` x `size` top-down RGBA8 image with a transparent background.
    // False when the SubTool has no drawn slot (hidden, not synced yet, out of GPU memory). Keeps the caller's
    // framebuffer and viewport.
    bool renderSubtoolThumbnail(uint32_t id, float yaw, float pitch, int size, const RenderSettings& settings,
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
    // viewResolveFbo_ is a single-sample RGBA8 copy: a multisample resolve must match formats exactly, and the
    // window's own framebuffer can be another format (10-bit or sRGB on some Windows drivers), so the resolve
    // goes there first and a plain blit (which may convert) takes it to the window. If even that blit errors,
    // directView_ makes later frames draw straight to the window without antialiasing.
    GLuint viewFbo_ = 0, viewResolveFbo_ = 0, viewRb_[3] = {};
    int viewW_ = 0, viewH_ = 0, viewSamples_ = 0, viewFailed_ = 0;
    bool directView_ = false;
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
        // PBR view (RenderSettings::Shading::Pbr): the texture's material channels packed (pbrViewTexels) as
        // ORM, normal and emissive textures; `pbrPlanes` says which are uploaded (pbrViewPlanes bits). A material
        // normal uses tangents computed here (MikkTSpace) unless a baked normal map preview brings its own.
        GLuint ormTexture = 0, matNormalTexture = 0, emissiveTexture = 0;
        uint32_t pbrPlanes = 0;
        uint64_t pbrSerial = 0;
        std::vector<glm::vec4> matTangents; // per mesh corner
        uint64_t matTangentKey = 0;         // UV fingerprint + counts they were computed for
        bool tangentsFromMaterial = false;  // tangentTexture holds matTangents (not the baked preview's)
        bool hasTangents = false, tangentsStale = true, normalFlipGreen = false;
        bool hasColors = false, hasMask = false, hasGroups = false, hasUvs = false;
        const MeshOverlay* overlay = nullptr; // this frame's (SceneMesh::overlay)
        uint64_t overlaySerial = 0;           // the one uploaded
        bool overlayColors = false;           // the per-triangle data holds overlay colours, not polygroups
        bool fresh = true; // nothing uploaded yet
        bool regrid = false; // a stroke's vertex left the position grid (prepareStrokeUpload): the next sync re-sends everything
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
        std::vector<AABB> blockBox;      // per UploadOrder block: its vertices' box as last packed
        // Clusters whose culling bounds a stroke upload set from blockBox (a sphere round the box, no normal cone,
        // the old volume): refineStrokeBounds makes them exact.
        std::vector<uint32_t> looseClusters;
        std::vector<uint8_t> looseMark; // per cluster: in looseClusters
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
        // A mesh with a micro level (core/Micro.h) draws through this instead of the buffers above.
        std::shared_ptr<MicroRenderer> micro;
        size_t microDrawn = 0;
    };
    MeshSlot& slot(uint32_t id); // created on first use
    static void freeSlot(MeshSlot& s);
    void sync(MeshSlot& s, Mesh& mesh, const NormalMapView* normalMap = nullptr, bool wireframe = false, bool pbr = false);
    void uploadVertices(MeshSlot& s, const Mesh& mesh, bool full);
    // `computed`: the bounds are already current (only the totals are left).
    void updateBounds(MeshSlot& s, const Mesh& mesh, bool all, bool computed = false);
    void buildLines(MeshSlot& s, const Mesh& mesh);
    void buildProxies(MeshSlot& s, const Mesh& mesh);
    bool proxyUsed_ = false; // the last draw() drew motion proxies: sync keeps them current
    bool proxyPending_ = false; // (proxiesPending)
    // The ghost (setGhost), drawn on the slot ghostId_.
    GhostStroke ghost_;
    uint32_t ghostId_ = 0;
    struct MeshProgram;
    void setGhostUniforms(const MeshProgram& p);
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
    void syncTexture(MeshSlot& s, Mesh& mesh, bool pbr);
    void uploadTangents(MeshSlot& s, const std::vector<glm::vec4>& all);
    // Image-based lighting (core/EnvironmentMath.h): the prefiltered environment (RGB16F, a mip per roughness
    // level), the BRDF table (RG16F) and the SH9, built when the environment first shows or changes.
    bool ensureEnvironment(int index);
    void setPbrUniforms(const MeshProgram& p, const RenderSettings& settings, bool textured, const MeshSlot& s);
    GLuint envTexture_ = 0, brdfTexture_ = 0, envBgProgram_ = 0;
    struct EnvJob;
    std::shared_ptr<EnvJob> envJob_;
    std::function<void()> envWake_;
    int envIndex_ = -1, envFailedIndex_ = -1;
    SH9 envSh_;
    glm::mat3 frameViewToEnv_{1.0f};
    // The clusters of `s` to draw this frame: in full (drawClusters_), and with `proxy` as their motion proxy
    // where it is built (proxyClusters_).
    void cull(const MeshSlot& s, const CullView& view, bool proxy = false);
    void drawSlot(const MeshSlot& s, bool active, bool flat, const RenderSettings& settings);
    enum class Part { Triangles, Lines, Proxy };
    void drawPatches(const MeshSlot& s, GLenum mode, Part part, GLint primBaseLocation);
    void drawClusters(const MeshSlot& s, const std::vector<uint32_t>& clusters, GLenum mode, Part part, GLint primBaseLocation);
    std::vector<uint32_t> proxyClusters_;
    std::vector<GLsizei> drawCounts_;
    std::vector<const void*> drawOffsets_;
    std::vector<GLint> drawBases_;
    std::vector<uint32_t> drawClusters_;
    std::vector<uint32_t> extraClusters_[4]; // drawSlot: clusters a ghost dab reaches (1), a draft moved (2), both
    uint32_t draftId_ = 0;                   // setDraftNormals
    bool cullBackfaces_ = false; // the last cull(): back faces can't show (so the GPU may skip them too)
    bool forceBackfaces_ = false; // this frame: RenderSettings::backfaceCulling (#68), back faces never drawn
    std::vector<uint32_t> cullScratch_;
    size_t lastDrawn_ = 0;
    // The frame draw() is drawing: matrices, the view's rectangle (x, y, w, h) and its culling.
    glm::mat4 frameView_{1.0f}, frameProj_{1.0f};
    glm::vec4 frameViewport_{0.0f};
    CullView frameCull_;
    // Mesh shader variants by feature bits (kPaint, kPrim, kTextured, kNormalMap, kFlat, kPbr), compiled on first use.
    struct MeshProgram {
        GLuint id = 0;
        GLint view = -1, proj = -1, gridOrigin = -1, gridStep = -1, viewport = -1, projScale = -1, baseColor = -1, paint = -1,
              showMask = -1, groups = -1, matcap = -1, matcapCavity = -1, dim = -1, primBase = -1, hasPaint = -1,
              normalFlipY = -1, twoSided = -1, checker = -1, checkerScale = -1, tintShift = -1;
        GLint hasOrm = -1, hasEmissive = -1, hasMatNormal = -1, envLevels = -1, exposure = -1, toneMap = -1, material = -1,
              pbrCavity = -1, viewToEnv = -1, sh = -1;
        GLint ghostGroups = -1, ghostRange = -1, ghostEdge = -1, ghostA = -1, ghostB = -1, ghostC = -1, ghostBound = -1, ghostKind = -1, ghostFocal = -1;
    };
    static constexpr int kPaint = 1, kPrim = 2, kTextured = 4, kNormalMap = 8, kFlat = 16, kMicro = 32, kGhost = 64,
                         kPbr = 128;
    MeshProgram programs_[256];
    void drawMicro(const MeshSlot& s, bool active, bool flat, const RenderSettings& settings);
    std::string programError_;
    const MeshProgram* meshProgram(int features);
    bool vertexIdHasBase_ = true; // gl_VertexID includes the base vertex (checked at init; else per-cluster draws)
    bool checkVertexIdBase();
    // (A list: a slot stays put while others come and go, so prepareStrokeUpload can work on one from another thread.)
    std::list<MeshSlot> slots_;
    struct DrawItem {
        uint32_t id;
        bool active;
        bool flat;
    };
    std::vector<DrawItem> drawList_;      // visible meshes, in scene order
    std::vector<uint32_t> visibleGroups_; // scratch for uploadFaces
    std::vector<glm::vec2> visibleUvs_;   // scratch for uploadFaces
    std::vector<GLuint> matcapTextures_; // one per matcap (core/Matcap.h): the built-ins, then custom ones
    std::vector<uint64_t> matcapHashes_; // the custom matcap pixels each texture holds
    uint64_t matcapRevision_ = 0;
    void syncMatcaps(); // uploads custom matcaps added or changed since the last call (syncScene and draw call it)
    static void uploadMatcap(GLuint texture, int size, const std::vector<uint8_t>& pixels);
};

} // namespace sf
