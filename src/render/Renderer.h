// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/EnvironmentMath.h"
#include "core/LitSphere.h"
#include "core/Mesh.h"
#include "core/Reference.h"
#include "core/Splats.h"
#include "core/UploadOrder.h"
#include "render/Camera.h"
#include "render/Clusters.h"
#include "render/CoarseFollower.h"
#include "render/GL.h"
#include "render/Ghost.h"
#include "render/ScreenStroke.h"

#include <cstdint>
#include <cstdlib>
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
    bool showVertexPaint = true;   // vertex colours replace baseColor when the mesh has them
    // The active colour tints the Lit Sphere material on meshes with no vertex paint (Color palette, as the classic
    // main colour does): materialTint multiplies the material there, white leaves it as it is (materialTintFor).
    bool colorTintsMaterial = true;
    glm::vec3 materialTint{1.0f};
    bool showTexture = true;     // with showVertex paint: a Part's painted texture (core/Texture.h) multiplies in
    bool showMask = true;        // masked areas render darker
    float maskDarkness = 0.7f;   // how dark: 0 = no tint .. 1 = darkest (Preferences > Interface > Mask darkness)
    bool maskColorCustom = false; // masked areas blend towards maskColor (linear) instead of darkening (Mask colour)
    glm::vec3 maskColor{0.20f, 0.22f, 0.30f};
    bool showFaceGroups = false; // colour each face group (see face groupColor)
    int litsphere = defaultLitSphere(); // built-in Lit Sphere material (core/Lit Sphere.h), Soft Clay; -1 = studio clay lighting
    bool showMesh = true;
    bool showSplats = true;
    float splatOpacity = 1.0f; // global fade for using a capture as reference
    bool dimInactive = true;   // Parts other than the active one render slightly darker
    bool motionProxy = false;  // the view is moving (Fast Navigation): draw big meshes' lighter proxy (render/Clusters.h)
    double stillSeconds = 0.0; // how long the view has been still (0 while it moves): coarse levels give way to full detail
    bool stroking = false;     // a stroke runs (its dabs still landing): coarse levels stay
    double frameClock = 0.0;   // seconds on the frame clock (scripted in UI tests): the coarse levels follow edits after a rest
    // Offline frames (the timelapse, --render): always the full mesh, never its coarse levels. In the view a frame
    // too slow for the still budget stays coarse while edited clusters draw in full until their levels follow, so
    // a timelapse seeking through the history flipped between the two from frame to frame (Discord, 0.11.10).
    bool fullDetail = false;
    // Unlit (#168): every surface shows its base colour (vertex paint, texture, face groups) with no lighting, Lit Sphere
    // or PBR shading, for judging painted colour as stored. The mask tint, wireframe and the dimming stay.
    bool unlit = false;
    bool flatShading = false;  // the scene's shading (Material palette): faceted; Parts can override (core/Document.h)
    // Backface culling (#68, Tool > Viewport Look): back faces are never drawn, on any mesh (closed or not, from any
    // view). Off: they are skipped only where they can't show (a closed mesh seen from outside, Renderer::cull).
    bool backfaceCulling = false;
    // Floor grid (#115, View > Floor grid): a faint grid on the floor under the model (y = floorHeight, Y up; the
    // app keeps it at the lowest point of the visible Parts), its X and Z axes through the origin, fading with
    // distance. Drawn under every Part. Off by default.
    bool floorGrid = false;
    float floorHeight = 0.0f;
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
    // Viewport shading: Lit Sphere (the lit sphere or studio lights above) or PBR, which shades the material channels
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
    // Lit Sphere and studio lighting (#80): the light turned from where the material has it, in degrees about the
    // view's up axis (yaw, + to the right) and its side axis (pitch, + up), its intensity (a multiplier) and an
    // ambient term that lifts the shadows (a share of the colour). The defaults leave the material as it is.
    float lightYaw = 0.0f, lightPitch = 0.0f;
    float lightIntensity = 1.0f, lightAmbient = 0.0f;
    // X-Ray (#80, #91): the other visible Parts are drawn see-through (only their nearest surface, at
    // xrayOpacity), so the active Part shows through them; with no other Part shown, the active one is see-through.
    bool xray = false;
    float xrayOpacity = 0.3f;
    // The 3D view's resolution as a share of the window's (#80): lower draws fewer pixels on slow GPUs and scales
    // the picture up (Renderer::drawView). 1 = full resolution.
    float resolutionScale = 1.0f;
    bool pbr() const { return shading == Shading::Pbr; }
};

// RenderSettings::lightYaw / lightPitch as a rotation of view-space directions (the shader's uLightRot).
glm::mat3 lightRotation(float yawDegrees, float pitchDegrees);

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

// One mesh of the scene for Renderer::syncScene: a Part, keyed by its stable id. Linked Parts (#94) share one
// mesh: they come with the same id (their PartData key) and their own `model` matrices, and are drawn from one
// GPU slot, uploaded once.
struct SceneMesh {
    uint32_t id = 0;
    Mesh* mesh = nullptr;
    bool active = false; // gets mask darkening, face group colours and wireframe; never dimmed
    bool visible = true; // hidden meshes keep their GPU slot but aren't drawn (or uploaded)
    const NormalMapView* normalMap = nullptr; // shade with this normal map (needs the mesh's UVs)
    bool wireframe = false; // keep the wireframe's edges on the GPU (built on first need; draw() falls back to
                            // outlining the triangles, quad diagonals included, until they are)
    bool flat = false;      // flat shading: each triangle shows its own face normal (faceted)
    bool pbr = false;       // the view shades PBR: upload the texture's material channels and their tangents
    const MeshOverlay* overlay = nullptr; // the UV Editor's view of the active mesh (must outlive the frame)
    glm::mat4 model{1.0f}; // where the mesh is in the scene (Part::transform)
};

// Part of the target the 3D view is drawn into (framebuffer pixels, origin bottom-left); the rest is
// cleared to `surround`. w == 0: the whole target.
struct RenderViewport {
    int x = 0, y = 0, w = 0, h = 0;
    glm::vec3 surround{0.118f, 0.118f, 0.125f};
};

// Display colour of a face group id (linear RGB): a stable hash spread over
// pleasant hues. The mesh shader uses the same formula.
glm::vec3 facegroupColor(uint32_t id);

// Draws every visible Part, each from its own GPU buffers (a slot keyed by
// Part id), in clusters (core/UploadOrder.h) culled on the CPU every frame
// (render/Clusters.h) and drawn with one multi-draw. Mesh drawing honours the optional Mesh attributes: colours
// (vertex paint), mask (darkening), face groups (group colours) and hidden
// triangles (not drawn). Mark changed vertices dirty for colour/mask edits and
// set Mesh::facesDirty after changing `hidden` or `face groups`. Only what a
// mesh marks dirty is uploaded, so inactive Parts upload once.
class Renderer {
public:
    bool init(std::string& error);
    void shutdown();
    // The warm UI test server (app/UiServe.h): shaders linked once and kept for the next Renderer in the same GL
    // context (and the driver check init makes); false frees them.
    static void keepPrograms(bool keep);
    // The next simpler mesh shader variant (the same feature bits meshProgram takes) for a driver that rejects one:
    // drops the fanciest bit left, in the order ghost, PBR, normal map, texture, per-triangle data, flat. Returns
    // `features` unchanged when nothing more can go (then the mesh isn't drawn, and the log says why).
    static int simplerMeshFeatures(int features) {
        for (int bit : {kGhost, kPbr, kNormalMap, kTextured, kPrim, kFlat})
            if (features & bit) return features & ~bit;
        return features;
    }
    // Makes `scene` what draw() shows, in order: uploads whatever each visible
    // mesh marked dirty and clears its dirty state (a new slot uploads
    // everything), and frees the slots of ids no longer listed.
    void syncScene(const std::vector<SceneMesh>& scene);
    // True when Part `id` didn't fit in GPU memory at its last full upload (it isn't drawn until it does).
    bool outOfMemory(uint32_t id) const;
    // Full mesh uploads so far (a whole layout and every vertex): UI tests check that an undo sends only what it changed.
    uint64_t fullUploads() const { return fullUploads_; }
    // Re-uploads the splats when `version` differs from the last upload.
    void syncSplats(const SplatCloud& splats, uint64_t version);
    using Viewport = RenderViewport;
    void draw(const Camera& camera, int fbWidth, int fbHeight, const RenderSettings& settings, const Viewport& viewport = {});
    // draw() for the app's window (whose framebuffer has no multisampling): through an offscreen target with
    // `samples` samples, resolved to the screen, or straight to the screen for 1. When the GPU can't make the
    // target (e.g. out of memory) it uses fewer samples. Returns the samples used.
    int drawView(const Camera& camera, int fbWidth, int fbHeight, const RenderSettings& settings, const Viewport& viewport,
                 int samples);
    // Reference images (core/Reference.h) for the next draws: Behind ones over the background, In the scene ones
    // after the Parts (depth tested, so the model hides them, never writing depth). The window's view shows every
    // shown one; renderToImage (renders, exports, the timelapse) only those with inRenders, thumbnails none.
    void setReferences(const std::vector<ReferenceImage>& refs); // (their GPU memory: referenceGpuBytes)
    // Offscreen render (4x MSAA) into top-down RGBA8 pixels.
    bool renderToImage(const Camera& camera, int width, int height, const RenderSettings& settings,
                       std::vector<uint8_t>& rgba, std::string& error);

    // Persistent offscreen target (4x MSAA) for recording: draw between these calls.
    bool beginCapture(int width, int height, std::string& error);
    void endCapture(std::vector<uint8_t>& rgba); // top-down RGBA rows

    // PBR view: an environment's lighting is being built (the view shows the previous one meanwhile). `wake` is
    // called from the worker thread when it is ready (the app redraws then).
    bool environmentPending() const;
    // The PBR planes Part `id` has on the GPU (pbrViewPlanes bits: 1 ORM, 2 normal, 4 emissive), for tests.
    uint32_t pbrPlanes(uint32_t id) const;
    void setEnvironmentWake(std::function<void()> wake) { envWake_ = std::move(wake); }

    // GL texture of the painted texture of Part `id` as last synced (0 if none), for UI previews (rows bottom-up).
    GLuint paintTexture(uint32_t id) const;

    // --- Strokes on heavy meshes (app/AsyncStroke.cpp) ---
    // The predictive stroke preview (render/Ghost.h) drawn on Part `id`; an empty stroke turns it off.
    void setGhost(uint32_t id, const GhostStroke& ghost);
    // Research (render/ScreenStroke.h, SF_SCREEN_STROKE=1): the same pending dabs applied to the drawn view instead
    // of the geometry (Lit Sphere views through drawView only); an empty stroke turns it off.
    void setScreenStroke(const GhostStroke& stroke) { screenStroke_ = stroke; }
    // The last drawView applied the screen stroke.
    bool screenStrokeDrawn() const { return screenStrokeDrawn_; }
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
        size_t bytes() const { return packed * sizeof(PackedVertex); }
        size_t packed = 0; // vertices packed (in data, or the staging buffer)
        // Persistent upload (SF_PERSISTENT_UPLOAD=1, a research prototype): bindStrokeUpload lends a persistently
        // mapped staging buffer; prepare packs straight into it when the batch fits (`staged`), and commit has
        // the GPU copy the ranges into the vertex buffer (no glBufferSubData of the packed data on the GL thread).
        PackedVertex* stagingPtr = nullptr;
        size_t stagingCap = 0; // vertices
        int stagingIndex = -1;
        bool staged = false;
        PackedVertex* dst() { return staged ? stagingPtr : data.data(); }
    };
    // `curvature` (optional, by vertex): the cavity term of every vertex the mesh marked dirty, already worked
    // out (Sculptor::strokeCurvature).
    bool prepareStrokeUpload(uint32_t id, Mesh& mesh, StrokeUpload& out, const float* curvature = nullptr);
    // GL thread, before handing `up` to another thread: finds Part id's slot (slots come and go on the GL
    // thread, e.g. thumbnails; the other thread uses this one, which stays put in the list).
    void bindStrokeUpload(uint32_t id, StrokeUpload& up);
    void setPersistentUpload(bool on) { persistentUpload_ = on; } // Fast Upload (Preferences > Performance, experimental)
    void commitStrokeUpload(StrokeUpload& up);
    // The stroke uploads gave the clusters they moved quick culling bounds (around their vertices' boxes, which
    // packing finds anyway, without the normal cone, so they are never culled as facing away); at the stroke's
    // end this works out the exact ones (GL thread, the mesh no longer changing).
    void refineStrokeBounds(uint32_t id, const Mesh& mesh);
    // A draft stroke on Part `id` (0: none): the clusters its uploads moved draw with per-pixel face normals
    // (theirs are stale) until refineStrokeBounds.
    void setDraftNormals(uint32_t id);
    // refineStrokeBounds on the solver thread at a stroke's end, after prepareStrokeUpload: the exact bounds go
    // into `up` and commitStrokeUpload sets them.
    void refineStrokeBoundsInto(uint32_t id, const Mesh& mesh, StrokeUpload& up);

    // Bytes of GPU buffers and textures the mesh slots hold.
    size_t gpuBytes() const;
    // Micro grid points whose shading still waits for a frame's budget (MicroRenderer::pendingShading, all slots).
    size_t microPendingShading() const;
    // Focus Detail's GPU sync time by part since the last call (MicroFocus::Part; adds into out[MicroFocus::kParts]).
    void takeFocusSplit(double* out);
    // Triangles the last draw() sent to the GPU (after culling), for the benchmark.
    size_t lastDrawnTriangles() const { return lastDrawn_; }
    bool floorGridDrawn() const { return floorGridDrawn_; } // the last draw() drew the floor grid (tests)
    // Coarse levels (tests): the clusters the last frame drew at a coarse level, and the edited clusters whose levels
    // are still to be updated (drawn in full until then).
    size_t coarseDrawnClusters() const { return lastCoarseClusters_; }
    size_t underlayDrawnClusters() const { return underlayClusters_.size(); } // (edited clusters in full over their coarse level, the last Part drawn)
    size_t coarsePendingClusters() const;
    // How often the active Part's drawn detail (full, or the coarse level it drew) changed between two frames when
    // either was a stroke frame: a stroke must not flip the surface between detail levels (#help: fast Clay strokes
    // alternated full and coarse detail on a 5M mesh).
    size_t strokeDetailSwitches() const { return strokeDetailSwitches_; }
    // Dynamic topology patches (tests): the triangles of mesh slot `id`'s uploaded patches whose indices on the GPU
    // (read back) aren't the mesh's triangles now.
    size_t patchErrors(uint32_t id, const Mesh& mesh);
    size_t patchCount(uint32_t id) const {
        for (const MeshSlot& s : slots_)
            if (s.id == id) return s.patches.size();
        return 0;
    }
    // The stroke-only programs (predictive preview, draft normals) compiled so far: drawSlot warms them before a stroke.
    int strokePrograms() const {
        int n = 0;
        for (int f = 0; f < 256; ++f) n += programs_[f].id && (f & (kGhost | kFlat)) ? 1 : 0;
        return n;
    }
    // At rest (still 150 ms, no stroke) an over-budget mesh draws in full detail when one full frame fits the still-frame
    // budget (100 ms, from the measured GPU time per triangle); else it stays coarse (coarsePreviewOnly: the info line
    // says so). coarseStillWanted: the last frame drew coarse and full detail follows once the view has been still long
    // enough (the app draws until then).
    bool coarsePreviewOnly() const { return previewOnly_; }
    bool coarseStillWanted() const { return stillWanted_; }
    // The frame governor's GPU timer: the whole frame's GPU time, the CPU's before the 3D view drew (render/Clusters.h:
    // nextGpuMsPerTriangle)
    void noteGpuFrame(double gpuMs, uint64_t drawnTriangles, double cpuBeforeDrawMs = 0.0);
    void setTriangleRate(double millionsPerSecond) { triangleRateM_ = millionsPerSecond; } // (calibration; 0 = unknown)
    void setStroking(bool on) { stroking_ = on; } // a stroke runs: sync leaves the edited clusters' levels until it ends
    // The last syncScene left motion proxies to build (a few milliseconds of them a frame; the rest draw in full).
    bool proxiesPending() const { return proxyPending_; }
    // Build stale motion proxies on still frames too (after a stroke, so the next orbit starts with them), at a
    // smaller budget; only for meshes whose proxies were drawn before. The app turns it on when nothing sculpts.
    void setProxyPrebuild(bool on) { proxyPrebuild_ = on; }

    // Texture of lit sphere `index` (sRGB RGBA8, rows bottom-up) for UI previews; 0 if none (or a custom one not
    // uploaded yet: the next syncScene or draw does).
    GLuint litsphereTexture(int index) const;

    // --- Thumbnails (Library / pickers) ---
    // Renders `mesh` alone (not the synced scene, which is left as it was) into a `size` x `size`
    // top-down RGBA8 image with a transparent background. Keeps the caller's framebuffer and viewport.
    bool renderMeshThumbnail(Mesh& mesh, const Camera& camera, int size, const RenderSettings& settings,
                             std::vector<uint8_t>& rgba, std::string& error);
    // Part thumbnails (#43): Part `id` alone as last synced, from the GPU buffers it already has (nothing
    // uploaded, so it stays cheap on 30M+ meshes: those draw their motion proxy where built), framed from the
    // angle `yaw`, `pitch` (degrees) into a `size` x `size` top-down RGBA8 image with a transparent background.
    // False when the Part has no drawn slot (hidden, not synced yet, out of GPU memory). Keeps the caller's
    // framebuffer and viewport.
    bool renderPartThumbnail(uint32_t id, float yaw, float pitch, int size, const RenderSettings& settings,
                                std::vector<uint8_t>& rgba, std::string& error);

private:
    void drawFloorGrid(const Camera& camera, const glm::mat4& view, const glm::mat4& proj,
                       const RenderSettings& settings);
    void drawSplats(const glm::mat4& view, const glm::mat4& proj, int fbWidth, int fbHeight, float opacity);

    GLuint wireProgram_ = 0, wireLineProgram_ = 0, bgProgram_ = 0, splatProgram_ = 0;
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
    struct Staging { // the persistent upload's buffers (StrokeUpload::stagingPtr), used in turn
        GLuint buffer = 0;
        void* ptr = nullptr;
        size_t cap = 0; // vertices
        GLsync fence = nullptr;
    } persist_[2];
    int stagingNext_ = 0;
    bool persistentUpload_ = false; // setPersistentUpload
    bool persistentUpload() const;  // the preference, SF_PERSISTENT_UPLOAD=0 / 1 overrides; needs GL_ARB_buffer_storage
    int captureW_ = 0, captureH_ = 0;
    GLuint emptyVao_ = 0;
    bool thumbnailPass_ = false; // draw(): transparent background, no gradient (renderMeshThumbnail)
    std::vector<ReferenceImage> references_; // setReferences
    struct ReferenceTexture {
        std::shared_ptr<const Image> image; // (kept: the pointer stays the key while the texture lives)
        GLuint tex = 0;
    };
    std::vector<ReferenceTexture> referenceTextures_;
    GLuint referenceProgram_ = 0;
    bool viewReferences_ = true; // false inside renderToImage: only the references marked for renders
    void drawReferences(bool behind, const glm::mat4& view, const glm::mat4& proj, bool ortho, float aspect);
    // The GPU copy of one mesh (one Part), in the cluster layout of `order`.
    struct MeshSlot {
        uint32_t id = 0;
        GLuint vao = 0, lineVao = 0;
        GLuint vertexBuffer = 0; // PackedVertex per GPU slot (render/Clusters.h)
        GLuint indexBuffer = 0;  // 16-bit, relative to each cluster's first vertex; kClusterTriangles * 3 per cluster
        GLuint lineBuffer = 0;   // wireframe edges, 16-bit like the triangles, each edge once
        GLuint colorBuffer = 0, maskBuffer = 0; // RGBA8 (gamma encoded), R8
        GLuint groupData = 0, groupTexture = 0; // face group id per drawn triangle (texture buffer, R16UI or R32UI)
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
        bool overlayColors = false;           // the per-triangle data holds overlay colours, not face groups
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
        // Coarse levels (buildCoarseLevels at the first sync over the budget, again after a new layout). Clusters an
        // edit moved draw in full (coarseDirty) until the first sync without new edits updates their levels.
        GLuint coarseVao = 0, coarseVertices = 0, coarseIndices = 0, coarseColors = 0, coarseMask = 0; // (colours RGBA8, mask R8)
        std::unique_ptr<CoarseLevels> coarse;
        std::vector<uint8_t> coarseDirty;
        std::vector<uint32_t> coarsePending; // the dirty clusters, to update
        bool coarseTried = false, coarseEdited = false; // (coarseEdited: this sync marked some)
        std::unique_ptr<CoarseFollower> follow; // the coarse cells follow a stroke from its uploads (a thread of its own)
        bool followStale = false;               // the mesh changed in ways the follower didn't see (the next rest starts it again)
        std::vector<uint32_t> coarseFresh; // the clusters edits marked since the last sync (their cells follow at once)
        double coarseEditAt = 0.0; // frame clock of the last edit that marked a cluster dirty (the levels follow after a rest)
        mutable bool drewCoarse = false; // the last frame drew this Part on its coarse levels (a stroke keeps what it started on)
        std::vector<uint32_t> drawnOrder; // the mesh triangle of each drawn triangle (meshes with UVs only)
        size_t drawnTriangles = 0;
        size_t vertexCapacity = 0;
        std::vector<uint16_t> sentCurvature; // per vertex, the cavity bits it last went up with (kKeepCurvature)
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
            std::vector<uint32_t> groups;    // their face groups, while stale
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
        // The stroke-only programs (GHOST, draft FLAT) warmed for these features (drawSlot; -1: not yet), and
        // the frames drawn while waiting (the open's first frame isn't made slower).
        mutable int strokeWarmed = -1, strokeWarmFrames = 0;
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
    bool proxyPrebuild_ = false; // (setProxyPrebuild)
    double proxyMsPerCluster_ = 0.0; // measured, so a frame's batches fit its budget on slow cores too
    // Coarse levels (docs/design/micro-coarse-lod.md; SF_COARSE_LOD=0 turns them off): plain meshes over the triangle
    // budget draw every visible cluster at one coarse level (render/Clusters.h: CoarseLevels), the finest that fits the
    // budget; full detail when it all fits.
    const bool coarseLod_ = !(std::getenv("SF_COARSE_LOD") && std::string(std::getenv("SF_COARSE_LOD")) == "0");
    void markCoarseDirty(MeshSlot& s, uint32_t c);
    void updateCoarse(MeshSlot& s, const Mesh& mesh);
    void followEdits(MeshSlot& s, const Mesh& mesh); // the fresh clusters' cells, while the Part is on its coarse levels
    size_t lodBudget_ = 0; // the last still draw's triangle budget: sync builds the coarse levels of meshes over it
    void buildCoarse(MeshSlot& s, const Mesh& mesh);
    void uploadCoarsePoints(MeshSlot& s);
    void applyFollow(MeshSlot& s);              // the coarse follower's latest cells into the levels and the GPU
    void syncFollow(MeshSlot& s, const Mesh& mesh); // starts it (or again after edits it didn't see) once the levels are current
    void uploadCoarseAttributes(MeshSlot& s); // the cells' colours and mask (with the coarse VAO bound)
    size_t triangleBudget(const RenderSettings& settings) const;
    // The ghost (setGhost), drawn on the slot ghostId_.
    GhostStroke ghost_;
    uint32_t ghostId_ = 0;
    struct MeshProgram;
    void setGhostUniforms(const MeshProgram& p);
    GhostStroke screenStroke_;
    ScreenStroke screenPass_;
    bool screenStrokeDrawn_ = false;
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
    std::vector<std::pair<uint32_t, uint32_t>> regridRuns_; // the dirty runs before a new position grid re-sent everything
    bool regridded_ = false;                               // this sync made a new grid (the coarse levels follow regridRuns_ only)
    void syncTexture(MeshSlot& s, Mesh& mesh, bool pbr);
    void uploadTangents(MeshSlot& s, const std::vector<glm::vec4>& all);
    // Image-based lighting (core/EnvironmentMath.h): the prefiltered environment (RGB16F, a mip per roughness
    // level), the BRDF table (RG16F) and the SH9, built when the environment first shows or changes.
    bool ensureEnvironment(int index);
    void setPbrUniforms(const MeshProgram& p, const RenderSettings& settings, bool textured, const MeshSlot& s);
    void setLightUniforms(const MeshProgram& p, const RenderSettings& settings) const;
    float drawAlpha_ = 1.0f;     // the opacity the next meshes draw with (X-Ray's see-through Parts)
    bool scaleFailed_ = false;   // the scaled view couldn't reach the window: full resolution from then on
    bool keepBackfaces_ = false;
    GLenum xrayCull_ = 0;        // X-Ray's lone Part: the faces this pass culls (GL_FRONT, then GL_BACK) // X-Ray's lone see-through Part: draw its back faces too (cull)
    GLuint envTexture_ = 0, brdfTexture_ = 0, envBgProgram_ = 0;
    GLuint floorProgram_ = 0;   // drawFloorGrid's, linked when the grid first shows
    bool floorGridDrawn_ = false;
    struct EnvJob;
    std::shared_ptr<EnvJob> envJob_;
    std::function<void()> envWake_;
    int envIndex_ = -1, envFailedIndex_ = -1;
    SH9 envSh_;
    glm::mat3 frameViewToEnv_{1.0f};
    // The clusters of `s` to draw this frame: in full (drawClusters_), and with `proxy` as their motion proxy
    // where it is built (proxyClusters_).
    void cull(const MeshSlot& s, const CullView& view, bool proxy = false);
    // The coarse-level cull: every visible cluster in full (drawClusters_) if that fits `budget`, else at one level
    // (coarseClusters_, coarseLevel_), the finest that fits.
    void cullCoarse(const MeshSlot& s, const CullView& view, size_t budget);
    std::vector<uint32_t> coarseClusters_;
    // The edited clusters drawn in full beside the coarse rest also draw their coarse level, pushed back (cullCoarse): it
    // closes the cracks between the full ones and their coarse neighbours, which meet at the cells' points.
    std::vector<uint32_t> underlayClusters_;
    float underlayPush_ = 0.0f;
    int underlayLevel_ = 0;
    size_t lastCoarseClusters_ = 0;
    size_t strokeDetailSwitches_ = 0;
    double clock_ = 0.0;              // RenderSettings::frameClock of the last draw
    int detailNow_ = 0;               // the active Part's last frame: 0 = full, else 1 + the coarse level
    bool detailWasStroking_ = false;
    bool previewOnly_ = false, stillWanted_ = false, stroking_ = false;
    uint64_t fullUploads_ = 0;
    double gpuMsPerTriangle_ = 0.0; // measured (moving average), 0 = not yet
    double triangleRateM_ = 0.0;    // calibrated, million triangles a second (0 = unknown)
    std::vector<uint8_t> coarseLevel_;
    void drawSlot(const MeshSlot& s, bool active, bool flat, const RenderSettings& settings);
    enum class DrawPart { Triangles, Lines, Proxy };
    void drawPatches(const MeshSlot& s, GLenum mode, DrawPart part, GLint primBaseLocation);
    void drawClusters(const MeshSlot& s, const std::vector<uint32_t>& clusters, GLenum mode, DrawPart part, GLint primBaseLocation);
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
        bool failed = false; // the driver rejected this variant (logged once): draws use a simpler one
        GLint view = -1, proj = -1, gridOrigin = -1, gridStep = -1, viewport = -1, projScale = -1, baseColor = -1, paint = -1,
              showMask = -1, maskDarkness = -1, maskColor = -1, groups = -1, litsphere = -1, litsphereCavity = -1, dim = -1, primBase = -1,
              hasPaint = -1, unlit = -1, normalFlipY = -1, twoSided = -1, checker = -1, checkerScale = -1, tintShift = -1, lightRot = -1,
              lightLevels = -1, alpha = -1, microWire = -1, pushBack = -1;
        GLint hasOrm = -1, hasEmissive = -1, hasMatNormal = -1, envLevels = -1, exposure = -1, toneMap = -1, material = -1,
              pbrCavity = -1, viewToEnv = -1, sh = -1;
        GLint ghostGroups = -1, ghostRange = -1, ghostEdge = -1, ghostA = -1, ghostB = -1, ghostC = -1, ghostD = -1, ghostBound = -1, ghostKind = -1, ghostFocal = -1;
    };
    static constexpr int kPaint = 1, kPrim = 2, kTextured = 4, kNormalMap = 8, kFlat = 16, kMicro = 32, kGhost = 64,
                         kPbr = 128;
    static constexpr int kFocus = kPaint; // with kMicro: a streamed level (render/MicroFocus.h; micro programs never paint)
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
        bool moved = false;      // drawn with `model` (a linked copy, #94)
        glm::mat4 model{1.0f};
    };
    // drawSlot for `item`: with its model matrix in the frame's view and culling (restored after).
    void drawItem(const MeshSlot& s, const DrawItem& item, const RenderSettings& settings);
    std::vector<DrawItem> drawList_;      // visible meshes, in scene order
    std::vector<uint32_t> visibleGroups_; // scratch for uploadFaces
    std::vector<glm::vec2> visibleUvs_;   // scratch for uploadFaces
    std::vector<GLuint> litsphereTextures_; // one per lit sphere (core/Lit Sphere.h): the built-ins, then custom ones
    std::vector<uint64_t> litsphereHashes_; // the custom lit sphere pixels each texture holds
    uint64_t litsphereRevision_ = 0;
    void syncLitSpheres(); // uploads custom lit spheres added or changed since the last call (syncScene and draw call it)
    static void uploadLitSphere(GLuint texture, int size, const std::vector<uint8_t>& pixels);
};

} // namespace sf
