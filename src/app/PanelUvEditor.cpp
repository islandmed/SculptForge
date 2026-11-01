// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The UV Editor (Tool > Texture Map > UV Editor, Alt+U): a window with the active Part's UV layout in 2D,
// drawn on the GPU (render/UvView.h) over the UDIM tile grid with a checker or the painted texture behind it.
//
// - Select vertices, edges, faces or islands (1-4) by clicking or dragging a box (Shift adds, Ctrl removes), or
//   on the model with Select in 3D; move, rotate and scale them with the box's handles or G / R / S.
// - Mark and clear seams on the model with the Seam tool (drag along edges; Alt clears), or from face groups,
//   island borders or sharp edges; Unwrap (least stretch, conformal, by face groups, planar, unfold, auto seams),
//   Pack into one tile or across UDIM tiles, set or match texel density, lay islands into trim sheet strips,
//   pin, weld, split, straighten and flip.
// - The model shows the same colours live: islands, stretch or texel density, the selection and a checker, so
//   distortion shows on the surface while the UVs are edited; with "Paint stays on the model" a painted texture
//   moves with every edit (core/TextureTransfer.h), so the model keeps its look.
//
// Every operation goes through the uv_* commands (core/UvCommands.cpp) or one UvEditStep (core/UvEdit.h), so it
// is one undo step and scriptable; big unwraps run in the background.
#include "app/App.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include "core/Texture.h"
#include "core/Uv.h"
#include "core/UvEdit.h"
#include "render/UvView.h"

#include <IconsLucide.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <future>
#include <limits>
#include <thread>

namespace sf {

namespace {

constexpr const char* kPath = "UV Editor";
constexpr int kSizes[] = {1024, 2048, 4096, 8192};
constexpr const char* kSizeLabels[] = {"1K", "2K", "4K", "8K"};
constexpr UvUnwrapMethod kMethods[] = {UvUnwrapMethod::Isometric, UvUnwrapMethod::Conformal, UvUnwrapMethod::Polygroups,
                                       UvUnwrapMethod::Planar,    UvUnwrapMethod::Unfold,    UvUnwrapMethod::Auto};
constexpr const char* kColorLabels[] = {"Islands", "Stretch", "Density", "Plain"};
constexpr const char* kImageLabels[] = {"No Image", "Checker", "Texture"};

bool ctrlKey(SDL_Keymod mod) { return (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }

// RGBA8 as the GPU and ImGui read it (0xAABBGGRR).
uint32_t pack(glm::vec3 c, float a) {
    auto b = [](float x) { return uint32_t(std::lround(std::clamp(x, 0.0f, 1.0f) * 255.0f)); };
    return b(c.x) | b(c.y) << 8 | b(c.z) << 16 | b(a) << 24;
}

// A pleasant, stable colour per island (golden-ratio hues, soft saturation).
glm::vec3 islandColor(uint32_t i) {
    const float h = std::fmod(0.07f + float(i) * 0.61803399f, 1.0f);
    const glm::vec3 rgb = glm::clamp(glm::abs(glm::fract(glm::vec3(h) + glm::vec3(0.0f, 2.0f / 3.0f, 1.0f / 3.0f)) * 6.0f - 3.0f) -
                                         1.0f,
                                     0.0f, 1.0f);
    return glm::mix(glm::vec3(1.0f), rgb, 0.55f) * (0.78f + 0.16f * float((i * 7u) % 3u) / 2.0f);
}

const glm::vec3 kSelect(1.0f, 0.55f, 0.16f); // the accent, for the selection
const glm::vec3 kSeam(0.96f, 0.30f, 0.26f);

float distToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 d = b - a;
    const float l = glm::dot(d, d);
    const float t = l > 0.0f ? std::clamp(glm::dot(p - a, d) / l, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + d * t));
}

bool insideTriangle(glm::vec2 p, glm::vec2 a, glm::vec2 b, glm::vec2 c) {
    const float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    const float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    const float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

} // namespace

struct UvEditorState {
    UvViewRenderer gpu;
    bool gpuTried = false;
    std::string gpuError;
    // The view: the UV point at its centre and pixels per UV unit (0: frame on the next draw).
    glm::vec2 center{0.5f};
    float zoom = 0.0f;
    ImVec2 viewMin{0, 0}, viewSize{0, 0};
    bool hovered = false;
    // Display.
    int color = 0;  // kColorLabels
    int image = 1;  // kImageLabels
    float imageOpacity = 0.55f;
    float checkerScale = 8.0f;
    bool modelColors = true;
    bool modelChecker = true;
    bool keepTexture = true; // paint stays on the model
    int sizeChoice = 1;      // texture size for density and margins without a texture
    // Caches of the active mesh's UVs, rebuilt when they change.
    uint32_t subtool = ~0u;
    uint64_t revision = ~uint64_t(0);
    const void* uvData = nullptr;
    size_t uvCount = 0, flagCount = 0;
    bool dirty = true;        // everything again
    bool colorsDirty = true;  // colours only (selection, display)
    bool hasUvs = false;
    UvTopology topo;
    UvStretchCache stretchCache;
    std::vector<float> stretch, density;
    float meanDensity = 0.0f, meanStretch = 0.0f, flipped = 0.0f;
    std::vector<uint32_t> faceCol, edgeCol, edges;
    std::vector<uint8_t> moving, faceSel;
    std::vector<uint32_t> selCorners; // what the selection moves
    glm::vec2 selLo{0.0f}, selHi{0.0f};
    size_t selIslands = 0;
    float selDensity = 0.0f;
    glm::ivec2 tiles{1, 1};
    std::vector<int> tileList;
    MeshOverlay overlay;
    // Interaction.
    enum class Drag { None, Pan, Box, Move, Rotate, Scale };
    Drag drag = Drag::None;
    bool modal = false; // started by G / R / S: follows the mouse until a click
    ImVec2 pressAt{0, 0};
    glm::vec2 pressUv{0.0f}, pivot{0.0f};
    int axis = 0; // 1: u only, 2: v only
    glm::mat3 live{1.0f};
    std::vector<glm::vec2> orig; // dragged corners' UVs at the start
    std::unique_ptr<UvEditStep> step;
    double lastLive = 0.0;
    bool boxAdd = false, boxRemove = false;
    // Panel options.
    int method = 0; // kMethods
    int iterations = 10;
    float marginPx = 8.0f;
    int packTiles = 1;
    bool rotate = true;
    bool equalDensity = true;
    float targetDensity = 512.0f;
    char strips[64] = "4";
    int strip = 0;
    bool fitWidth = false;
    bool trimOpen = false; // the Trim Sheet section is open: the strips show in the view
    float seamAngle = 60.0f;
    // The 3D tool: 0 off, 1 Seams, 2 Select.
    int tool = 0;
    bool toolClearing = false, toolAdding = true;
    uint32_t toolVertex = ~0u;
    std::unique_ptr<UvEditStep> toolStep;
    std::vector<uint32_t> twin; // cornerTwins of the mesh the tool works on
    uint64_t twinKey = 0;
    // Seams drawn on the model.
    std::vector<uint32_t> seamCorners;
    uint64_t seamKey = ~uint64_t(0);
    // Background unwrap.
    struct Job {
        std::future<bool> done;
        std::shared_ptr<std::atomic<float>> progress;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<UvUnwrapResult> result;
        std::shared_ptr<std::string> error;
        uint32_t subtool = 0;
        size_t triangles = 0;
        UvUnwrapMethod method = UvUnwrapMethod::Isometric;
        bool keepTexture = true;
    };
    std::unique_ptr<Job> job;
    int unwraps = 0; // finished (UI tests)
    double frameMs = 0.0; // what the last frame of the window cost on the CPU (UI tests: uv_frame_ms)
};

// The UV Editor's work, with App's internals (a friend of App).
struct UvEditorUi {
    App& app;
    UvEditorState& s;
    Mesh& mesh() { return app.doc_.mesh; }
    int resolution() {
        const Mesh& m = mesh();
        return m.texture ? std::max(m.texture->width, m.texture->height) : kSizes[std::clamp(s.sizeChoice, 0, 3)];
    }

    // --- Caches ---------------------------------------------------------------------------------------------

    void refresh() {
        const Mesh& m = mesh();
        const uint32_t id = app.doc_.activeSubtoolInfo().id;
        if (s.drag != UvEditorState::Drag::None) return; // (the drag keeps its own caches up to date)
        if (id != s.subtool || app.doc_.undo.revision() != s.revision || m.uvs.data() != s.uvData || m.uvs.size() != s.uvCount ||
            m.uvFlags.size() != s.flagCount)
            s.dirty = true;
        if (!s.dirty && !s.colorsDirty) return;
        if (s.dirty) rebuildTopology();
        rebuildColors();
        s.dirty = s.colorsDirty = false;
    }

    void rebuildTopology() {
        Mesh& m = mesh();
        s.subtool = app.doc_.activeSubtoolInfo().id;
        s.revision = app.doc_.undo.revision();
        s.uvData = m.uvs.data();
        s.uvCount = m.uvs.size();
        s.flagCount = m.uvFlags.size();
        s.hasUvs = m.hasUvs() && m.uvs.size() == m.triangleCount() * 3;
        if (!app.doc_.uvSelection.valid(m)) {
            const UvSelectMode mode = app.doc_.uvSelection.mode;
            app.doc_.uvSelection.clear();
            app.doc_.uvSelection.mode = mode;
        }
        if (!s.hasUvs) {
            s.topo = UvTopology();
            s.stretch.clear();
            s.density.clear();
            if (s.gpu.triangles()) s.gpu.setUvs({});
            s.tiles = {1, 1};
            s.tileList.clear();
            return;
        }
        s.topo = buildUvTopology(m);
        s.stretchCache = uvStretchCache(m, s.topo);
        const size_t nt = m.triangleCount();
        s.stretch.resize(nt);
        for (uint32_t t = 0; t < nt; ++t) s.stretch[t] = uvTriangleStretchAt(m, t, s.topo, s.stretchCache);
        uvTriangleDensity(m, resolution(), s.density);
        s.meanDensity = uvMeshDensity(m, resolution());
        double sum = 0.0, w = 0.0, flipped = 0.0;
        for (uint32_t t = 0; t < nt; ++t) {
            const glm::vec2* u = m.uvs.data() + size_t(t) * 3;
            const double a = 0.5 * std::abs(double((u[1].x - u[0].x) * (u[2].y - u[0].y) - (u[2].x - u[0].x) * (u[1].y - u[0].y)));
            w += a;
            if (s.stretch[t] < 0.0f) flipped += a;
            else sum += a * double(s.stretch[t]);
        }
        s.meanStretch = w > 0.0 ? float(sum / w) : 0.0f;
        s.flipped = w > 0.0 ? float(flipped / w) : 0.0f;
        // Edges: island borders once per side, inner edges once.
        s.edges.clear();
        for (uint32_t c = 0; c < s.topo.twin.size(); ++c)
            if (s.topo.border[c] || c < s.topo.twin[c]) s.edges.push_back(c);
        // UDIM tiles in use.
        s.tileList.clear();
        glm::ivec2 hi(1, 1);
        for (uint32_t i = 0; i < s.topo.islandCount; ++i) {
            const uint32_t t = s.topo.islandTriangles[s.topo.islandOffsets[i]];
            const glm::vec2 c = (m.uvs[t * 3] + m.uvs[t * 3 + 1] + m.uvs[t * 3 + 2]) / 3.0f;
            const int tile = udimTile(c);
            s.tileList.push_back(tile);
            const glm::vec2 o = udimOrigin(tile);
            hi = glm::max(hi, glm::ivec2(o) + 1);
        }
        std::sort(s.tileList.begin(), s.tileList.end());
        s.tileList.erase(std::unique(s.tileList.begin(), s.tileList.end()), s.tileList.end());
        s.tiles = hi;
        s.gpu.setUvs(m.uvs);
        s.gpu.setEdges(s.edges);
        s.moving.assign(m.uvs.size(), 0);
    }

    // Faces the selection covers (all of them in face modes; those touching a selected corner otherwise).
    void selectionFaces() {
        const Mesh& m = mesh();
        const UvSelection& sel = app.doc_.uvSelection;
        const size_t nt = m.triangleCount();
        s.faceSel.assign(nt, 0);
        if (!sel.valid(m)) return;
        if (sel.mode == UvSelectMode::Face || sel.mode == UvSelectMode::Island) {
            s.faceSel = sel.faces;
        } else {
            for (uint32_t c = 0; c < sel.corners.size(); ++c)
                if (sel.corners[c]) s.faceSel[c / 3] = 2; // touched
        }
    }

    void rebuildColors() {
        const Mesh& m = mesh();
        const size_t nt = m.triangleCount(), nc = nt * 3;
        if (!s.hasUvs) {
            s.overlay.colors.clear();
            s.overlay.checker = false;
            ++s.overlay.serial;
            return;
        }
        const UvSelection& sel = app.doc_.uvSelection;
        selectionFaces();
        s.selCorners = uvSelectedCorners(m, s.topo, sel);
        s.selLo = glm::vec2(std::numeric_limits<float>::max());
        s.selHi = glm::vec2(std::numeric_limits<float>::lowest());
        for (uint32_t c : s.selCorners) s.selLo = glm::min(s.selLo, m.uvs[c]), s.selHi = glm::max(s.selHi, m.uvs[c]);
        const std::vector<uint8_t> isl = uvSelectedIslands(m, s.topo, sel, false);
        s.selIslands = size_t(std::count(isl.begin(), isl.end(), uint8_t(1)));
        {
            const std::vector<float> d = uvIslandDensity(m, s.topo, resolution());
            double sum = 0.0, w = 0.0;
            for (uint32_t i = 0; i < s.topo.islandCount; ++i) {
                if (s.selIslands && !isl[i]) continue;
                const double k = double(s.topo.islandOffsets[i + 1] - s.topo.islandOffsets[i]);
                sum += double(d[i]) * k, w += k;
            }
            s.selDensity = w > 0.0 ? float(sum / w) : 0.0f;
        }
        s.faceCol.resize(nc);
        s.edgeCol.resize(nc);
        const bool colors3d = s.modelColors && s.color != 3;
        s.overlay.colors.assign(colors3d || std::any_of(s.faceSel.begin(), s.faceSel.end(), [](uint8_t f) { return f; }) ? nt : 0, 0);
        const float faint = nt > 300000 ? 0.10f : 0.18f;
        for (uint32_t t = 0; t < nt; ++t) {
            glm::vec3 c(0.85f);
            float a = 0.13f;
            switch (s.color) {
            case 0: c = islandColor(s.topo.island[t]), a = 0.40f; break;
            case 1: c = s.stretch[t] < 0.0f ? glm::vec3(0.85f, 0.2f, 0.85f) : uvHeatColor(std::clamp(s.stretch[t] * 3.0f, 0.0f, 1.0f)), a = 0.72f; break;
            case 2: {
                const float x = s.meanDensity > 0.0f && s.density[t] > 0.0f ? std::log2(s.density[t] / s.meanDensity) : 0.0f;
                c = uvHeatColor(x * 1.5f), a = 0.72f;
                break;
            }
            default: break;
            }
            glm::vec3 model = c;
            if (s.faceSel[t] == 1) model = glm::mix(c, kSelect, s.color == 1 || s.color == 2 ? 0.55f : 0.85f), c = model, a = 0.66f;
            else if (s.faceSel[t] == 2) model = glm::mix(model, kSelect, 0.45f);
            const uint32_t fc = pack(c, a);
            s.faceCol[t * 3] = s.faceCol[t * 3 + 1] = s.faceCol[t * 3 + 2] = fc;
            if (!s.overlay.colors.empty())
                s.overlay.colors[t] = (pack(colors3d || s.faceSel[t] ? model : glm::vec3(0.62f, 0.58f, 0.53f), 1.0f) & 0x00FFFFFFu);
        }
        const bool flags = m.uvFlags.size() == nc;
        std::vector<uint8_t> hitVertex;
        if (sel.valid(m) && sel.mode == UvSelectMode::Vertex) {
            hitVertex.assign(s.topo.uvVertexCount, 0);
            for (uint32_t c = 0; c < nc; ++c)
                if (sel.corners[c]) hitVertex[s.topo.uvVertex[c]] = 1;
        }
        for (uint32_t c = 0; c < nc; ++c) {
            bool selected = false;
            if (sel.valid(m) && sel.mode == UvSelectMode::Edge) selected = sel.corners[c] || (s.topo.twin[c] != kNoCorner && !s.topo.border[c] && sel.corners[s.topo.twin[c]]);
            if (!hitVertex.empty()) selected = hitVertex[s.topo.uvVertex[c]] && hitVertex[s.topo.uvVertex[cornerNext(c)]];
            if (selected) s.edgeCol[c] = pack(kSelect, 1.0f);
            else if (flags && (m.uvFlags[c] & kUvSeam)) s.edgeCol[c] = pack(kSeam, 0.95f);
            else if (s.topo.border[c]) s.edgeCol[c] = pack(glm::vec3(0.93f), 0.82f);
            else s.edgeCol[c] = pack(glm::vec3(0.9f), faint);
        }
        std::fill(s.moving.begin(), s.moving.end(), uint8_t(0));
        s.moving.resize(nc, 0);
        s.gpu.setColors(s.faceCol, s.edgeCol, s.moving);
        s.overlay.checker = s.modelChecker;
        s.overlay.checkerScale = float(resolution()) / 32.0f; // a square per 32 texels, as the texture will hold them
        ++s.overlay.serial;
    }

    // --- View ----------------------------------------------------------------------------------------------

    ImVec2 toScreen(glm::vec2 uv) const {
        return ImVec2(s.viewMin.x + s.viewSize.x * 0.5f + (uv.x - s.center.x) * s.zoom,
                      s.viewMin.y + s.viewSize.y * 0.5f - (uv.y - s.center.y) * s.zoom);
    }
    glm::vec2 toUv(ImVec2 p) const {
        return glm::vec2(s.center.x + (p.x - s.viewMin.x - s.viewSize.x * 0.5f) / s.zoom,
                         s.center.y - (p.y - s.viewMin.y - s.viewSize.y * 0.5f) / s.zoom);
    }
    void frame(glm::vec2 lo, glm::vec2 hi) {
        if (s.viewSize.x <= 0 || s.viewSize.y <= 0) return;
        const glm::vec2 size = glm::max(hi - lo, glm::vec2(1e-4f));
        s.center = (lo + hi) * 0.5f;
        s.zoom = std::min(s.viewSize.x / size.x, s.viewSize.y / size.y) * 0.88f;
    }
    void frameAll() {
        const Mesh& m = mesh();
        if (!s.selCorners.empty() && !app.doc_.uvSelection.empty()) {
            frame(s.selLo, s.selHi);
            return;
        }
        glm::vec2 lo(0.0f), hi(float(s.tiles.x), float(s.tiles.y));
        for (const glm::vec2& u : m.uvs) lo = glm::min(lo, u), hi = glm::max(hi, u);
        frame(lo, hi);
    }

    // --- Picking -------------------------------------------------------------------------------------------

    // The triangle under `uv` (the smallest when they overlap), or -1.
    int64_t faceAt(glm::vec2 uv) {
        const Mesh& m = mesh();
        const size_t nt = m.triangleCount();
        const size_t block = 1 << 15, blocks = (nt + block - 1) / block;
        std::vector<std::pair<float, int64_t>> best(blocks, {std::numeric_limits<float>::max(), -1});
        poolFor(nt, [&](size_t b, size_t e) {
            auto& r = best[b / block];
            for (size_t t = b; t < e; ++t) {
                const glm::vec2* u = m.uvs.data() + t * 3;
                if (!insideTriangle(uv, u[0], u[1], u[2]) || m.triangleHidden(uint32_t(t))) continue;
                const float a = std::abs((u[1].x - u[0].x) * (u[2].y - u[0].y) - (u[2].x - u[0].x) * (u[1].y - u[0].y));
                if (a < r.first) r = {a, int64_t(t)};
            }
        }, block);
        std::pair<float, int64_t> r{std::numeric_limits<float>::max(), -1};
        for (const auto& b : best)
            if (b.first < r.first) r = b;
        return r.second;
    }
    // The nearest corner within `radius` UV units (vertex mode), or the corner starting the nearest edge.
    int64_t cornerAt(glm::vec2 uv, float radius, bool edge) {
        const Mesh& m = mesh();
        const size_t nc = m.uvs.size();
        const size_t block = 1 << 15, blocks = (nc + block - 1) / block;
        std::vector<std::pair<float, int64_t>> best(blocks, {radius, -1});
        poolFor(nc, [&](size_t b, size_t e) {
            auto& r = best[b / block];
            for (size_t c = b; c < e; ++c) {
                const float d = edge ? distToSegment(uv, m.uvs[c], m.uvs[cornerNext(uint32_t(c))]) : glm::distance(uv, m.uvs[c]);
                if (d < r.first) r = {d, int64_t(c)};
            }
        }, block);
        std::pair<float, int64_t> r{radius, -1};
        for (const auto& b : best)
            if (b.first < r.first) r = b;
        return r.second;
    }

    // Applies a pick to the selection: faces (per triangle flags) or corners, replacing, adding or removing.
    void applyPick(const std::vector<uint8_t>& faces, const std::vector<uint8_t>& corners, bool add, bool remove) {
        Mesh& m = mesh();
        UvSelection& sel = app.doc_.uvSelection;
        const bool faceMode = sel.mode == UvSelectMode::Face || sel.mode == UvSelectMode::Island;
        std::vector<uint8_t>& dst = faceMode ? sel.faces : sel.corners;
        const std::vector<uint8_t>& src = faceMode ? faces : corners;
        const size_t n = faceMode ? m.triangleCount() : m.triangleCount() * 3;
        if (dst.size() != n || (!add && !remove)) dst.assign(n, 0);
        if (src.size() == n)
            for (size_t i = 0; i < n; ++i)
                if (src[i]) dst[i] = remove ? 0 : 1;
        if (sel.mode == UvSelectMode::Island) uvConvertSelection(m, s.topo, sel, UvSelectMode::Island);
        s.colorsDirty = true;
    }

    void clickSelect(glm::vec2 uv, bool add, bool remove) {
        Mesh& m = mesh();
        const UvSelection& sel = app.doc_.uvSelection;
        const size_t nt = m.triangleCount();
        std::vector<uint8_t> faces, corners;
        const float radius = 9.0f * app.uiScale_ / s.zoom;
        if (sel.mode == UvSelectMode::Face || sel.mode == UvSelectMode::Island) {
            const int64_t t = faceAt(uv);
            faces.assign(nt, 0);
            if (t >= 0) {
                if (sel.mode == UvSelectMode::Island) {
                    const uint32_t i = s.topo.island[size_t(t)];
                    for (uint32_t k = s.topo.islandOffsets[i]; k < s.topo.islandOffsets[i + 1]; ++k) faces[s.topo.islandTriangles[k]] = 1;
                } else {
                    faces[size_t(t)] = 1;
                }
            }
        } else {
            const int64_t c = cornerAt(uv, radius, sel.mode == UvSelectMode::Edge);
            corners.assign(nt * 3, 0);
            if (c >= 0) {
                if (sel.mode == UvSelectMode::Vertex) {
                    for (uint32_t k = 0; k < nt * 3; ++k)
                        if (s.topo.uvVertex[k] == s.topo.uvVertex[size_t(c)]) corners[k] = 1;
                } else {
                    corners[size_t(c)] = 1;
                }
            }
        }
        applyPick(faces, corners, add, remove);
    }

    void boxSelect(glm::vec2 a, glm::vec2 b, bool add, bool remove) {
        Mesh& m = mesh();
        const UvSelection& sel = app.doc_.uvSelection;
        const glm::vec2 lo = glm::min(a, b), hi = glm::max(a, b);
        const size_t nt = m.triangleCount();
        std::vector<uint8_t> faces, corners;
        if (sel.mode == UvSelectMode::Face || sel.mode == UvSelectMode::Island) {
            std::vector<uint32_t> list;
            uvFacesInRect(m, lo, hi, sel.mode == UvSelectMode::Island, list);
            faces.assign(nt, 0);
            for (uint32_t t : list) faces[t] = 1;
        } else {
            corners.assign(nt * 3, 0);
            auto in = [&](glm::vec2 p) { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y; };
            for (uint32_t c = 0; c < nt * 3; ++c)
                corners[c] = sel.mode == UvSelectMode::Vertex ? in(m.uvs[c]) : in(m.uvs[c]) && in(m.uvs[cornerNext(c)]);
            if (sel.mode == UvSelectMode::Vertex) { // whole UV vertices
                std::vector<uint8_t> hit(s.topo.uvVertexCount, 0);
                for (uint32_t c = 0; c < nt * 3; ++c)
                    if (corners[c]) hit[s.topo.uvVertex[c]] = 1;
                for (uint32_t c = 0; c < nt * 3; ++c) corners[c] = hit[s.topo.uvVertex[c]];
            }
        }
        applyPick(faces, corners, add, remove);
    }

    // --- Transforms ----------------------------------------------------------------------------------------

    bool beginTransform(UvEditorState::Drag kind, ImVec2 at, bool modal) {
        Mesh& m = mesh();
        if (s.selCorners.empty() || !s.hasUvs) return false;
        s.drag = kind;
        s.modal = modal;
        s.pressAt = at;
        s.pressUv = toUv(at);
        s.pivot = (s.selLo + s.selHi) * 0.5f;
        s.axis = 0;
        s.live = glm::mat3(1.0f);
        s.orig.resize(s.selCorners.size());
        for (size_t i = 0; i < s.selCorners.size(); ++i) s.orig[i] = m.uvs[s.selCorners[i]];
        const char* label = kind == UvEditorState::Drag::Move ? "Move UVs" : kind == UvEditorState::Drag::Rotate ? "Rotate UVs" : "Scale UVs";
        s.step = std::make_unique<UvEditStep>(app.doc_, label, s.keepTexture);
        std::fill(s.moving.begin(), s.moving.end(), uint8_t(0));
        for (uint32_t c : s.selCorners) s.moving[c] = 1;
        s.gpu.setColors(s.faceCol, s.edgeCol, s.moving);
        return true;
    }

    void updateTransform(ImVec2 at, SDL_Keymod mod) {
        const glm::vec2 uv = toUv(at);
        glm::mat3 mat(1.0f);
        const bool snap = ctrlKey(mod);
        if (s.drag == UvEditorState::Drag::Move) {
            glm::vec2 d = uv - s.pressUv;
            if (s.axis == 1) d.y = 0.0f;
            if (s.axis == 2) d.x = 0.0f;
            if (snap) d = glm::round(d * 64.0f) / 64.0f;
            mat[2] = glm::vec3(d, 1.0f);
        } else {
            const glm::vec2 a = s.pressUv - s.pivot, b = uv - s.pivot;
            glm::mat2 lin(1.0f);
            if (s.drag == UvEditorState::Drag::Rotate) {
                float ang = std::atan2(b.y, b.x) - std::atan2(a.y, a.x);
                if (snap) ang = glm::radians(std::round(glm::degrees(ang) / 15.0f) * 15.0f);
                lin = glm::mat2(std::cos(ang), std::sin(ang), -std::sin(ang), std::cos(ang));
            } else {
                float k = glm::length(a) > 1e-6f ? glm::length(b) / glm::length(a) : 1.0f;
                if (snap) k = std::max(std::round(k * 10.0f) / 10.0f, 0.1f);
                lin = glm::mat2(s.axis == 2 ? 1.0f : k, 0.0f, 0.0f, s.axis == 1 ? 1.0f : k);
            }
            const glm::vec2 t = s.pivot - lin * s.pivot;
            mat = glm::mat3(glm::vec3(lin[0], 0.0f), glm::vec3(lin[1], 0.0f), glm::vec3(t, 1.0f));
        }
        s.live = mat;
        // The mesh follows at once (the 2D view through the matrix, the model at a rate it can keep up with).
        Mesh& m = mesh();
        poolFor(s.selCorners.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) m.uvs[s.selCorners[i]] = glm::vec2(mat * glm::vec3(s.orig[i], 1.0f));
        }, 1 << 15);
        const double now = double(SDL_GetTicksNS()) * 1e-9;
        const double interval = m.triangleCount() > 200000 ? 0.06 : 0.0;
        if (now - s.lastLive >= interval) {
            s.lastLive = now;
            liveUpdate();
        }
    }

    // During a drag: the stretch of the moved triangles, on the model and in the view.
    void liveUpdate() {
        Mesh& m = mesh();
        std::vector<uint8_t> touched(m.triangleCount(), 0);
        for (uint32_t c : s.selCorners) touched[c / 3] = 1;
        if (s.color == 1) {
            poolFor(m.triangleCount(), [&](size_t b, size_t e) {
                for (size_t t = b; t < e; ++t) {
                    if (!touched[t]) continue;
                    s.stretch[t] = uvTriangleStretchAt(m, uint32_t(t), s.topo, s.stretchCache);
                    const glm::vec3 c = s.stretch[t] < 0.0f ? glm::vec3(0.85f, 0.2f, 0.85f) : uvHeatColor(std::clamp(s.stretch[t] * 3.0f, 0.0f, 1.0f));
                    if (s.faceSel[t] != 1) s.faceCol[t * 3] = s.faceCol[t * 3 + 1] = s.faceCol[t * 3 + 2] = pack(c, 0.72f);
                    if (!s.overlay.colors.empty()) s.overlay.colors[t] = pack(s.faceSel[t] ? glm::mix(c, kSelect, s.faceSel[t] == 1 ? 0.55f : 0.45f) : c, 1.0f) & 0x00FFFFFFu;
                }
            }, 1 << 14);
            s.gpu.setColors(s.faceCol, s.edgeCol, s.moving);
        }
        ++s.overlay.serial;
        m.facesDirty = true; // the model's UVs (checker, texture)
    }

    void endTransform(bool cancel) {
        Mesh& m = mesh();
        if (cancel) {
            for (size_t i = 0; i < s.selCorners.size(); ++i) m.uvs[s.selCorners[i]] = s.orig[i];
        }
        s.live = glm::mat3(1.0f);
        s.drag = UvEditorState::Drag::None;
        s.modal = false;
        std::string note;
        if (s.step) note = s.step->commit();
        s.step.reset();
        m.facesDirty = true;
        s.dirty = true;
        if (!cancel) app.status_ = "UVs moved" + note;
        else app.status_ = "Cancelled";
    }

    // --- Commands -------------------------------------------------------------------------------------------

    void run(const char* name, Args args = {}) {
        app.runCommand(name, args);
        s.dirty = true;
    }

    Args packArgs() {
        Args a{{"margin", double(s.marginPx)},
               {"tiles", double(s.packTiles)},
               {"rotate", s.rotate},
               {"equal_density", s.equalDensity},
               {"resolution", double(resolution())},
               {"keep_texture", s.keepTexture}};
        return a;
    }

    void unwrap() {
        Mesh& m = mesh();
        if (s.job || m.triangleCount() == 0) return;
        const UvUnwrapMethod method = kMethods[std::clamp(s.method, 0, 5)];
        Args a = packArgs();
        a["method"] = std::string(uvUnwrapMethodId(method));
        a["iterations"] = double(s.iterations);
        if (m.triangleCount() < 60000) { // quick enough to wait for
            run("uv_unwrap", a);
            ++s.unwraps;
            return;
        }
        auto job = std::make_unique<UvEditorState::Job>();
        job->progress = std::make_shared<std::atomic<float>>(0.0f);
        job->cancel = std::make_shared<std::atomic<bool>>(false);
        job->result = std::make_shared<UvUnwrapResult>();
        job->error = std::make_shared<std::string>();
        job->subtool = app.doc_.activeSubtoolInfo().id;
        job->triangles = m.triangleCount();
        job->method = method;
        job->keepTexture = s.keepTexture;
        auto copy = std::make_shared<Mesh>();
        copy->positions = m.positions;
        copy->triangles = m.triangles;
        copy->uvs = m.uvs;
        copy->uvFlags = m.uvFlags;
        copy->polygroups = m.polygroups;
        auto faces = std::make_shared<std::vector<uint8_t>>();
        const UvSelection& sel = app.doc_.uvSelection;
        if (sel.valid(m) && !sel.empty() && m.hasUvs()) {
            faces->assign(m.triangleCount(), 0);
            if (sel.mode == UvSelectMode::Face || sel.mode == UvSelectMode::Island) *faces = sel.faces;
            else
                for (uint32_t c : s.selCorners) (*faces)[c / 3] = 1;
        }
        UvUnwrapOptions o;
        o.method = method;
        o.resolution = resolution();
        o.iterations = s.iterations;
        o.pack = method != UvUnwrapMethod::Unfold;
        o.packing.resolution = resolution();
        o.packing.marginPx = s.marginPx;
        o.packing.tiles = s.packTiles;
        o.packing.rotate = s.rotate;
        o.packing.equalDensity = s.equalDensity;
        std::promise<bool> promise;
        job->done = promise.get_future();
        std::thread([promise = std::move(promise), copy, faces, o, progress = job->progress, cancel = job->cancel,
                     result = job->result, error = job->error]() mutable {
            UvUnwrapOptions opt = o;
            if (!faces->empty()) opt.faces = faces.get();
            const bool ok = uvUnwrap(*copy, opt, *result, *error, [&](float f) {
                progress->store(f);
                return !cancel->load();
            });
            promise.set_value(ok);
            wakeMainLoop();
        }).detach();
        s.job = std::move(job);
        app.status_ = std::string("Unwrapping (") + uvUnwrapMethodLabel(method) + ") " + std::to_string(m.triangleCount()) + " triangles...";
    }

    void poll() {
        if (!s.job) return;
        if (s.job->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            app.status_ = "Unwrapping: " + std::to_string(int(s.job->progress->load() * 100.0f)) + "%";
            return;
        }
        const std::unique_ptr<UvEditorState::Job> job = std::move(s.job);
        ++s.unwraps;
        if (!job->done.get()) {
            app.status_ = *job->error;
            return;
        }
        int index = -1;
        for (int i = 0; i < app.doc_.subtoolCount(); ++i)
            if (app.doc_.subtools[size_t(i)].id == job->subtool) index = i;
        if (index < 0 || app.doc_.subtoolMesh(index).triangleCount() != job->triangles) {
            app.status_ = "Unwrap skipped: the mesh changed while it was being unwrapped";
            return;
        }
        if (index != app.doc_.activeSubtool) app.doc_.selectSubtool(index);
        if (app.doc_.sculptor.strokeActive()) app.endStroke();
        UvEditStep step(app.doc_, std::string("Unwrap (") + uvUnwrapMethodLabel(job->method) + ")", job->keepTexture);
        mesh().uvs = std::move(job->result->uvs);
        const std::string note = step.commit();
        app.status_ = std::string("Unwrapped (") + uvUnwrapMethodLabel(job->method) + "): " +
                      std::to_string(job->result->islands) + " islands" + note;
        s.dirty = true;
    }

    // --- The 3D tool ----------------------------------------------------------------------------------------

    const std::vector<uint32_t>& twins() {
        const Mesh& m = mesh();
        const uint64_t key = (uint64_t(uintptr_t(m.triangles.data())) << 8) ^ m.triangles.size() ^ (uint64_t(app.doc_.activeSubtoolInfo().id) << 48);
        if (key != s.twinKey || s.twin.size() != m.triangleCount() * 3) {
            s.twin = cornerTwins(m);
            s.twinKey = key;
        }
        return s.twin;
    }

    uint32_t nearestVertex(const RayHit& hit) {
        const Mesh& m = mesh();
        const glm::uvec3 tri = m.triangles[hit.triangle];
        uint32_t best = tri.x;
        for (int j = 1; j < 3; ++j)
            if (glm::distance(m.positions[tri[j]], hit.position) < glm::distance(m.positions[best], hit.position)) best = tri[j];
        return best;
    }

    void toolSelectAt(const RayHit& hit, bool first) {
        Mesh& m = mesh();
        if (!s.hasUvs) return;
        const UvSelection& sel = app.doc_.uvSelection;
        const size_t nt = m.triangleCount();
        std::vector<uint8_t> faces, corners;
        const uint32_t t = hit.triangle;
        if (sel.mode == UvSelectMode::Face || sel.mode == UvSelectMode::Island) {
            faces.assign(nt, 0);
            if (sel.mode == UvSelectMode::Island) {
                const uint32_t i = s.topo.island[t];
                for (uint32_t k = s.topo.islandOffsets[i]; k < s.topo.islandOffsets[i + 1]; ++k) faces[s.topo.islandTriangles[k]] = 1;
            } else {
                faces[t] = 1;
            }
        } else {
            corners.assign(nt * 3, 0);
            if (sel.mode == UvSelectMode::Vertex) {
                const uint32_t v = nearestVertex(hit);
                for (uint32_t tt : m.vertexTriangles(v))
                    for (uint32_t j = 0; j < 3; ++j)
                        if (m.triangles[tt][j] == v) corners[tt * 3 + j] = 1;
            } else {
                float best = std::numeric_limits<float>::max();
                uint32_t bc = t * 3;
                for (uint32_t j = 0; j < 3; ++j) {
                    const glm::vec3 a = m.positions[m.triangles[t][j]], b = m.positions[m.triangles[t][(j + 1) % 3]];
                    const glm::vec3 d = b - a;
                    const float k = std::clamp(glm::dot(hit.position - a, d) / std::max(glm::dot(d, d), 1e-20f), 0.0f, 1.0f);
                    const float dist = glm::distance(hit.position, a + d * k);
                    if (dist < best) best = dist, bc = t * 3 + j;
                }
                corners[bc] = 1;
            }
        }
        applyPick(faces, corners, !first || s.toolAdding, !s.toolAdding);
    }
};

// ---------------------------------------------------------------------------------------------------------------

namespace {
UvEditorUi uvUi(App& app, std::shared_ptr<UvEditorState>& state) {
    if (!state) state = std::make_shared<UvEditorState>();
    return UvEditorUi{app, *state};
}
} // namespace

void App::registerUvEditorActions() {
    auto add = [this](const char* id, const char* label, const char* icon, const char* shortcut, std::function<void()> run,
                      std::function<bool()> checked, std::function<bool()> enabled, const char* description,
                      const char* command = "") {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = kPath;
        a.shortcut = shortcut;
        a.run = std::move(run);
        a.checked = std::move(checked);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    auto st = [this]() -> UvEditorState& {
        if (!uvEditor_) uvEditor_ = std::make_shared<UvEditorState>();
        return *uvEditor_;
    };
    auto uvs = [this] { return doc_.mesh.hasUvs(); };
    auto selected = [this] { return doc_.mesh.hasUvs() && doc_.uvSelection.valid(doc_.mesh) && !doc_.uvSelection.empty(); };
    auto runUv = [this](const char* name, Args a = {}) {
        runCommand(name, a);
        if (uvEditor_) uvEditor_->dirty = true;
    };
    add("ui.uv_editor", "UV Editor", ICON_LC_GRID_2X2, "Alt+U",
        [this] {
            showUvEditor_ = !showUvEditor_;
            if (showUvEditor_) { // the model to the left of the window, which opens over the right of the canvas
                const CanvasRect cr = canvasRect();
                camera_.frame(sceneBounds());
                camera_.zoom(-1.5f);
                camera_.pan(-cr.w * 0.33f, 0.0f, cr.h);
            }
        },
        [this] { return showUvEditor_; }, nullptr,
        "Open the UV Editor: the Part's UV layout in 2D over the UDIM tiles. Select vertices, edges, faces or islands "
        "(here or on the model), move, rotate and scale them, mark seams, unwrap, pack, set texel density and lay islands "
        "into trim sheet strips. Stretch and texel density show live on the model while you edit.");
    const char* modeIds[] = {"uv.mode_vertex", "uv.mode_edge", "uv.mode_face", "uv.mode_island"};
    const char* modeLabels[] = {"Vertex", "Edge", "Face", "Island"};
    const char* modeKeys[] = {"", "", "", ""};
    for (int i = 0; i < 4; ++i) {
        const UvSelectMode mode = UvSelectMode(i);
        add(modeIds[i], modeLabels[i], nullptr, modeKeys[i],
            [this, mode, runUv] { runUv("uv_select", {{"op", std::string("mode")}, {"mode", std::string(uvSelectModeId(mode))}}); },
            [this, mode] { return doc_.uvSelection.mode == mode; }, uvs,
            "Select UV vertices, edges, faces or whole islands (1-4 while the pointer is over the UV view). The "
            "selection converts when you switch.",
            "uv_select");
    }
    add("uv.unwrap", "Unwrap", ICON_LC_UNFOLD_HORIZONTAL, "", [this] { uvUi(*this, uvEditor_).unwrap(); }, nullptr,
        [this] { return doc_.mesh.triangleCount() > 0 && !(uvEditor_ && uvEditor_->job); },
        "Unwrap with the chosen method: the selected faces (fitted where they were), or the whole Part (packed). Least "
        "Stretch keeps angles and lengths along the marked seams; Conformal keeps angles; By Face Groups cuts along the "
        "face group borders too; Planar projects each piece; Unfold flattens the current islands again in place, keeping "
        "pins; Auto Seams finds its own. Closed pieces get cut automatically. Big meshes unwrap in the background.",
        "uv_unwrap");
    add("uv.pack", "Pack", ICON_LC_PACKAGE, "", [this] { uvUi(*this, uvEditor_).run("uv_pack", uvUi(*this, uvEditor_).packArgs()); },
        nullptr, uvs,
        "Pack the selected islands (or all) as large as they fit into the 0-1 tile, or across the chosen number of UDIM "
        "tiles: turned to their smallest box, scaled to one texel density, with the margin between them.",
        "uv_pack");
    add("uv.seam_mark", "Mark Seam", ICON_LC_SCISSORS, "", [runUv] { runUv("uv_seam", {{"op", std::string("mark")}}); }, nullptr,
        selected, "Mark the selected edges (or the border of the selected faces) as seams: Unwrap cuts there.", "uv_seam");
    add("uv.seam_clear", "Clear Seam", nullptr, "", [runUv] { runUv("uv_seam", {{"op", std::string("clear")}}); }, nullptr,
        selected, "Clear the seams on the selected edges (or the border of the selected faces).", "uv_seam");
    add("uv.seam_polygroups", "Face Groups", nullptr, "", [runUv] { runUv("uv_seam", {{"op", std::string("polygroups")}}); },
        nullptr, nullptr, "Mark every face group border as a seam.", "uv_seam");
    add("uv.seam_islands", "Island Borders", nullptr, "", [runUv] { runUv("uv_seam", {{"op", std::string("islands")}}); }, nullptr,
        uvs, "Mark every current UV island border as a seam (keeps an imported layout's cuts for Unwrap).", "uv_seam");
    add("uv.seam_angle", "Sharp Edges", nullptr, "",
        [this, runUv] { runUv("uv_seam", {{"op", std::string("angle")}, {"angle", double(uvEditor_ ? uvEditor_->seamAngle : 60.0f)}}); },
        nullptr, nullptr, "Mark every edge sharper than the angle as a seam (hard-surface models).", "uv_seam");
    add("uv.seam_clear_all", "Clear All", nullptr, "", [runUv] { runUv("uv_seam", {{"op", std::string("clear_all")}}); }, nullptr,
        nullptr, "Clear every marked seam.", "uv_seam");
    add("uv.tool_seams", "Seam Tool", ICON_LC_SPLINE, "",
        [this, st] { st().tool = st().tool == 1 ? 0 : 1; }, [this] { return uvEditor_ && uvEditor_->tool == 1; },
        [this] { return doc_.mesh.triangleCount() > 0; },
        "Draw seams on the model: drag along the edges you want cut (each stretch follows the shortest edge path); "
        "Alt+drag clears them. Seams show in red on the model while the UV Editor is open.");
    add("uv.tool_select", "Select in 3D", ICON_LC_BOX_SELECT, "",
        [this, st] { st().tool = st().tool == 2 ? 0 : 2; }, [this] { return uvEditor_ && uvEditor_->tool == 2; }, uvs,
        "Select on the model: click or drag over it to select faces, islands, vertices or edges (the UV Editor's mode); "
        "Alt removes. The selection shows in both views.");
    add("uv.pin", "Pin", ICON_LC_PIN, "", [runUv] { runUv("uv_edit", {{"op", std::string("pin")}}); }, nullptr, selected,
        "Pin the selected UVs: Unfold and Unwrap keep them where they are (P over the UV view).", "uv_edit");
    add("uv.unpin", "Unpin", ICON_LC_PIN_OFF, "", [runUv] { runUv("uv_edit", {{"op", std::string("unpin")}}); }, nullptr,
        selected, "Unpin the selected UVs (Alt+P over the UV view).", "uv_edit");
    add("uv.weld", "Weld", ICON_LC_MERGE, "",
        [this, runUv] { runUv("uv_edit", {{"op", std::string("weld")}, {"keep_texture", uvEditor_ ? uvEditor_->keepTexture : true}}); },
        nullptr, selected,
        "Weld the selected vertices: where they meet across a seam they become one UV each (the seam goes). W over the "
        "UV view.",
        "uv_edit");
    add("uv.split", "Split", ICON_LC_SPLIT, "", [runUv] { runUv("uv_edit", {{"op", std::string("split")}}); }, nullptr, selected,
        "Split the selection off: the selected edges, or the border of the selected faces, become seams so the faces move "
        "on their own (Y over the UV view).",
        "uv_edit");
    add("uv.straighten", "Straighten", ICON_LC_ALIGN_HORIZONTAL_JUSTIFY_CENTER, "",
        [this, runUv] { runUv("uv_edit", {{"op", std::string("straighten")}, {"keep_texture", uvEditor_ ? uvEditor_->keepTexture : true}}); },
        nullptr, selected, "Line the selected vertices up on one horizontal or vertical line (an edge loop, a border).",
        "uv_edit");
    add("uv.flip_u", "Flip U", ICON_LC_FLIP_HORIZONTAL, "",
        [this, runUv] { runUv("uv_edit", {{"op", std::string("flip_u")}, {"keep_texture", uvEditor_ ? uvEditor_->keepTexture : true}}); },
        nullptr, uvs, "Mirror the selected islands (or all) left to right.", "uv_edit");
    add("uv.flip_v", "Flip V", ICON_LC_FLIP_VERTICAL, "",
        [this, runUv] { runUv("uv_edit", {{"op", std::string("flip_v")}, {"keep_texture", uvEditor_ ? uvEditor_->keepTexture : true}}); },
        nullptr, uvs, "Mirror the selected islands (or all) top to bottom.", "uv_edit");
    add("uv.rotate_90", "Rotate 90", ICON_LC_ROTATE_CCW, "",
        [this, runUv] { runUv("uv_transform", {{"rotate", 90.0}, {"keep_texture", uvEditor_ ? uvEditor_->keepTexture : true}}); },
        nullptr, uvs, "Turn the selection (or everything) a quarter turn counter-clockwise.", "uv_transform");
    add("uv.density_set", "Set", nullptr, "",
        [this, runUv] {
            UvEditorState& s = *uvEditor_;
            runUv("uv_density", {{"op", std::string("set")}, {"value", double(s.targetDensity)},
                                 {"resolution", double(uvUi(*this, uvEditor_).resolution())}, {"keep_texture", s.keepTexture}});
        },
        nullptr, uvs, "Scale the selected islands (or all) to the target texel density.", "uv_density");
    add("uv.density_match", "Match", nullptr, "",
        [this, runUv] {
            runUv("uv_density", {{"op", std::string("match")}, {"resolution", double(uvUi(*this, uvEditor_).resolution())},
                                 {"keep_texture", uvEditor_->keepTexture}});
        },
        nullptr, uvs, "Give the selected islands (or all) one texel density: their mean, so their sizes match on the surface.",
        "uv_density");
    add("uv.density_pick", "Pick", nullptr, "", [this] { if (uvEditor_) uvEditor_->targetDensity = std::round(uvEditor_->selDensity); },
        nullptr, uvs, "Take the target texel density from the selected islands (or the whole Part).");
    add("uv.density_pack", "Pack at Density", nullptr, "",
        [this] {
            UvEditorUi u = uvUi(*this, uvEditor_);
            Args a = u.packArgs();
            a["density"] = double(uvEditor_->targetDensity);
            u.run("uv_pack", a);
        },
        nullptr, uvs,
        "Pack at exactly the target texel density, using as many UDIM tiles as it takes (1001, 1002, ...): every island "
        "keeps the same texels per unit across the tiles.",
        "uv_pack");
    add("uv.trim", "Assign to Strip", ICON_LC_STRETCH_HORIZONTAL, "",
        [this, runUv] {
            UvEditorState& s = *uvEditor_;
            runUv("uv_trim", {{"strip", double(s.strip + 1)}, {"strips", std::string(s.strips)}, {"fit_width", s.fitWidth},
                              {"resolution", double(uvUi(*this, uvEditor_).resolution())}});
        },
        nullptr, uvs,
        "Lay the selected islands into the chosen trim sheet strip: turned lengthwise, scaled to the strip's height and "
        "lined up along u. The texture stays put, so the islands pick up the strip's look.",
        "uv_trim");
    add("uv.frame", "Frame", ICON_LC_SCAN, "", [this] { uvUi(*this, uvEditor_).frameAll(); }, nullptr, nullptr,
        "Fit the selection (or the whole layout) in the UV view (F over it).");
}

bool App::uvEditorBusy() const { return uvEditor_ && uvEditor_->job; }

void App::pollUvEditor() {
    if (uvEditor_) uvUi(*this, uvEditor_).poll();
}

void App::uvEditorChanged() {
    if (uvEditor_) uvEditor_->dirty = true;
}

const MeshOverlay* App::uvEditorOverlay(uint32_t subtool) {
    if (!showUvEditor_ || !uvEditor_ || subtool != doc_.activeSubtoolInfo().id) return nullptr;
    if (!uvEditor_->hasUvs || uvEditor_->subtool != subtool) return nullptr;
    return &uvEditor_->overlay;
}

void App::drawUvEditor() {
    if (!showUvEditor_) {
        if (uvEditor_ && uvEditor_->tool) uvEditor_->tool = 0;
        return;
    }
    UvEditorUi u = uvUi(*this, uvEditor_);
    UvEditorState& s = *uvEditor_;
    const float scale = uiScale_;
    const Uint64 frameStart = SDL_GetTicksNS();
    if (!s.gpuTried) {
        s.gpuTried = true;
        if (!s.gpu.init(s.gpuError)) SDL_Log("UV Editor: %s", s.gpuError.c_str());
    }
    u.refresh();
    const CanvasRect cr = canvasRect();
    ImGui::SetNextWindowPos(ImVec2(cr.x + cr.w * 0.34f, cr.y + 6 * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(cr.w * 0.66f - 6 * scale, cr.h - 12 * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(520 * scale, 360 * scale), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::Begin("UV Editor", &showUvEditor_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }
    Mesh& m = doc_.mesh;
    const ImGuiStyle& style = ImGui::GetStyle();

    // Toolbar: selection mode, colours, image, the model overlay, frame.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Select");
    ImGui::SameLine();
    ui::Segmented(*this, {"uv.mode_vertex", "uv.mode_edge", "uv.mode_face", "uv.mode_island"}, 248 * scale);
    // The rest of the toolbar wraps onto a second line when the window is narrow.
    const float frameW = 74 * scale;
    const float rowStart = ImGui::GetCursorPosX();
    auto nextOnLine = [&](float itemW, float gap) {
        ImGui::SameLine(0, gap);
        if (ImGui::GetContentRegionAvail().x < itemW + frameW + style.ItemSpacing.x) {
            ImGui::NewLine();
            ImGui::SetCursorPosX(rowStart);
        }
    };
    nextOnLine(130 * scale, 14 * scale);
    ImGui::SetNextItemWidth(130 * scale);
    if (ImGui::Combo("##uvcolor", &s.color, kColorLabels, 4)) s.colorsDirty = true;
    ui::HelpText("Colours", nullptr,
                 "What the faces show, here and on the model: Islands (a colour each), Stretch (green: true to the surface; "
                 "yellow to red: stretched or squashed; magenta: folded over), Density (texel density; blue: fewer texels than the "
                 "mean, red: more) or Plain.",
                 kPath);
    nextOnLine(110 * scale, style.ItemSpacing.x);
    ImGui::SetNextItemWidth(110 * scale);
    ImGui::Combo("##uvimage", &s.image, kImageLabels, 3);
    ui::HelpText("Image", nullptr, "Behind the layout: nothing, a checker, or the painted texture (tile 1001).", kPath);
    nextOnLine(ImGui::CalcTextSize("Model").x + ImGui::GetFrameHeight() * 2, 14 * scale);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("On model");
    ImGui::SameLine();
    if (ui::Checkbox("Colours##uvmodel", &s.modelColors)) s.colorsDirty = true;
    ui::HelpText("Colours on the model", nullptr,
                 "Show the same colours on the model (islands, stretch, density and the selection), live while you edit.",
                 kPath);
    ImGui::SameLine();
    if (ui::Checkbox("Checker##uvchecker", &s.modelChecker)) s.colorsDirty = true;
    ui::HelpText("Checker on the model", nullptr,
                 "A checker on the model's UVs (tinted per UDIM tile): squares that stay square mean no stretch.", kPath);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - frameW);
    ui::ActionButton(*this, "uv.frame", ImVec2(frameW, 0));

    // Sidebar on the right, the view in the rest.
    const float sideW = 236 * scale;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float statusH = ImGui::GetTextLineHeightWithSpacing() + 2 * scale;
    const ImVec2 viewSize(std::max(avail.x - sideW - style.ItemSpacing.x, 64.0f), std::max(avail.y - statusH, 64.0f));
    const ImVec2 viewMin = ImGui::GetCursorScreenPos();
    s.viewMin = viewMin;
    s.viewSize = viewSize;
    if (s.zoom <= 0.0f) {
        u.frame(glm::vec2(0.0f), glm::vec2(float(s.tiles.x), float(s.tiles.y)));
        if (s.zoom <= 0.0f) s.zoom = 400.0f * scale;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("##uvview", viewSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                                     ImGuiButtonFlags_MouseButtonMiddle);
    s.hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 mouse = io.MousePos;
    const SDL_Keymod mod = SDL_GetModState();
    using D = UvEditorState::Drag;

    // The gizmo: the selection's box, its corners scale, the knob above rotates, inside moves.
    glm::vec2 boxLo = s.selLo, boxHi = s.selHi;
    const bool haveSel = s.hasUvs && !s.selCorners.empty() && !doc_.uvSelection.empty();
    auto gizmoCorners = [&](ImVec2 out[4]) {
        const glm::vec2 p[4] = {boxLo, {boxHi.x, boxLo.y}, boxHi, {boxLo.x, boxHi.y}};
        for (int i = 0; i < 4; ++i) out[i] = u.toScreen(glm::vec2(s.live * glm::vec3(p[i], 1.0f)));
    };
    auto handleAt = [&](ImVec2 p) -> D {
        if (!haveSel) return D::None;
        ImVec2 c[4];
        gizmoCorners(c);
        const float r = 7.0f * scale;
        for (int i = 0; i < 4; ++i)
            if (std::abs(p.x - c[i].x) <= r && std::abs(p.y - c[i].y) <= r) return D::Scale;
        const ImVec2 top((c[2].x + c[3].x) * 0.5f, std::min(c[2].y, c[3].y) - 22 * scale);
        if (std::hypot(p.x - top.x, p.y - top.y) <= r + 2 * scale) return D::Rotate;
        const float x0 = std::min(c[0].x, c[2].x), x1 = std::max(c[0].x, c[2].x), y0 = std::min(c[0].y, c[2].y), y1 = std::max(c[0].y, c[2].y);
        if (p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1) return D::Move;
        return D::None;
    };

    // Input over the view.
    if (s.hovered && io.MouseWheel != 0.0f && s.drag == D::None) {
        const glm::vec2 before = u.toUv(mouse);
        s.zoom = std::clamp(s.zoom * std::pow(1.18f, io.MouseWheel), 8.0f, 1e6f);
        const glm::vec2 after = u.toUv(mouse);
        s.center += before - after;
    }
    if (s.hovered && s.drag == D::None && !s.modal &&
        (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
         (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && spaceHeld_))) {
        s.drag = D::Pan;
    }
    if (s.drag == D::Pan) {
        s.center -= glm::vec2(io.MouseDelta.x, -io.MouseDelta.y) / s.zoom;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            s.drag = D::None;
    }
    if (s.modal) { // G / R / S: follows the mouse; click or Enter confirms, right click or Esc cancels
        u.updateTransform(mouse, mod);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) u.endTransform(false);
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) u.endTransform(true);
    } else if (s.hovered && s.drag == D::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && s.hasUvs) {
        const D h = handleAt(mouse);
        if (h != D::None && !(mod & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL))) {
            u.beginTransform(h, mouse, false);
        } else {
            s.pressAt = mouse;
            s.pressUv = u.toUv(mouse);
            s.boxAdd = (mod & SDL_KMOD_SHIFT) != 0;
            s.boxRemove = ctrlKey(mod);
            s.drag = D::Box;
        }
    }
    if (!s.modal && (s.drag == D::Move || s.drag == D::Rotate || s.drag == D::Scale)) {
        u.updateTransform(mouse, mod);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) u.endTransform(false);
    }
    if (s.drag == D::Box && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float moved = std::hypot(mouse.x - s.pressAt.x, mouse.y - s.pressAt.y);
        if (moved < 4.0f * scale) {
            u.clickSelect(s.pressUv, s.boxAdd, s.boxRemove);
            // A click on something already part of the selection then dragged moves it: handled by the gizmo.
        } else {
            u.boxSelect(s.pressUv, u.toUv(mouse), s.boxAdd, s.boxRemove);
        }
        s.drag = D::None;
        u.refresh();
    }

    // Draw: the GPU view, then the overlays.
    const glm::vec2 half = glm::vec2(viewSize.x, viewSize.y) * 0.5f / s.zoom;
    UvViewRenderer::View v;
    v.min = s.center - half;
    v.max = s.center + half;
    v.imageMode = s.image;
    v.image = s.image == 2 && hasTexture(m) ? renderer_.paintTexture(doc_.activeSubtoolInfo().id) : 0;
    v.imageOpacity = s.image == 2 ? 1.0f : s.imageOpacity;
    v.checkerScale = s.checkerScale;
    v.tiles = s.tiles;
    v.move = s.live;
    const int fbw = int(viewSize.x * io.DisplayFramebufferScale.x), fbh = int(viewSize.y * io.DisplayFramebufferScale.y);
    const GLuint tex = s.gpu.draw(fbw, fbh, v);
    const ImVec2 viewMax(viewMin.x + viewSize.x, viewMin.y + viewSize.y);
    if (tex) dl->AddImage(ImTextureID(uintptr_t(tex)), viewMin, viewMax, ImVec2(0, 1), ImVec2(1, 0));
    else dl->AddRectFilled(viewMin, viewMax, IM_COL32(25, 25, 27, 255));
    dl->PushClipRect(viewMin, viewMax, true);
    // UDIM tile numbers.
    for (int ty = 0; ty < std::max(s.tiles.y, 1) + 1; ++ty)
        for (int tx = 0; tx < std::min(std::max(s.tiles.x, 1) + 1, 10); ++tx) {
            const ImVec2 p = u.toScreen(glm::vec2(float(tx), float(ty) + 1.0f));
            const bool used = tx < s.tiles.x && ty < s.tiles.y;
            char label[8];
            std::snprintf(label, sizeof label, "%d", 1001 + tx + 10 * ty);
            dl->AddText(ImVec2(p.x + 5 * scale, p.y + 3 * scale), used ? IM_COL32(170, 170, 182, 210) : IM_COL32(110, 110, 118, 150), label);
        }
    // Trim strips (while the Trim Sheet section is open).
    if (s.trimOpen) {
        std::vector<TrimStrip> strips;
        std::string err;
        if (parseTrimStrips(s.strips, strips, err))
            for (size_t i = 0; i < strips.size(); ++i) {
                const ImVec2 a = u.toScreen(glm::vec2(0.0f, strips[i].v1)), b = u.toScreen(glm::vec2(1.0f, strips[i].v0));
                const bool on = int(i) == s.strip;
                dl->AddRectFilled(a, b, on ? IM_COL32(240, 136, 46, 38) : IM_COL32(120, 160, 255, 14));
                dl->AddRect(a, b, on ? IM_COL32(240, 136, 46, 200) : IM_COL32(150, 180, 255, 90), 0, 0, on ? 2.0f : 1.0f);
                dl->AddText(ImVec2(b.x - ImGui::CalcTextSize(strips[i].name.c_str()).x - 6 * scale, a.y + 3 * scale), on ? IM_COL32(255, 180, 110, 255) : IM_COL32(160, 180, 220, 200),
                            strips[i].name.c_str());
            }
    }
    // Pins and selected vertices.
    if (s.hasUvs && m.uvFlags.size() == m.uvs.size()) {
        size_t drawn = 0;
        for (uint32_t c = 0; c < m.uvFlags.size() && drawn < 5000; ++c)
            if ((m.uvFlags[c] & kUvPin) && s.topo.uvVertex.size() > c) {
                const ImVec2 p = u.toScreen(m.uvs[c]);
                dl->AddCircleFilled(p, 4.0f * scale, IM_COL32(90, 200, 255, 255));
                dl->AddCircle(p, 4.0f * scale, IM_COL32(10, 30, 50, 255));
                ++drawn;
            }
    }
    if (s.hasUvs && doc_.uvSelection.mode == UvSelectMode::Vertex && s.selCorners.size() < 60000) {
        for (uint32_t c : s.selCorners) {
            const ImVec2 p = u.toScreen(glm::vec2(s.live * glm::vec3(m.uvs[c], 1.0f)));
            if (s.drag == D::None || s.step == nullptr) dl->AddRectFilled(ImVec2(p.x - 2.5f * scale, p.y - 2.5f * scale), ImVec2(p.x + 2.5f * scale, p.y + 2.5f * scale), theme::u32(theme::accent));
        }
    }
    // The gizmo.
    if (haveSel && s.drag != D::Box) {
        ImVec2 c[4];
        if (s.drag != D::None) boxLo = s.selLo, boxHi = s.selHi;
        gizmoCorners(c);
        const ImU32 col = IM_COL32(255, 168, 80, 230);
        dl->AddPolyline(c, 4, col, ImDrawFlags_Closed, 1.2f * scale);
        const D hot = s.drag != D::None ? s.drag : s.hovered ? handleAt(mouse) : D::None;
        for (int i = 0; i < 4; ++i) {
            const float r = 4.5f * scale;
            dl->AddRectFilled(ImVec2(c[i].x - r, c[i].y - r), ImVec2(c[i].x + r, c[i].y + r), hot == D::Scale ? IM_COL32(255, 220, 160, 255) : col);
        }
        const ImVec2 topMid((c[2].x + c[3].x) * 0.5f, (c[2].y + c[3].y) * 0.5f);
        const ImVec2 knob(topMid.x, std::min(c[2].y, c[3].y) - 22 * scale);
        dl->AddLine(topMid, knob, col, 1.2f * scale);
        dl->AddCircleFilled(knob, 6.0f * scale, hot == D::Rotate ? IM_COL32(255, 220, 160, 255) : col);
        const ImVec2 mid((c[0].x + c[2].x) * 0.5f, (c[0].y + c[2].y) * 0.5f);
        dl->AddLine(ImVec2(mid.x - 6 * scale, mid.y), ImVec2(mid.x + 6 * scale, mid.y), col, 1.2f * scale);
        dl->AddLine(ImVec2(mid.x, mid.y - 6 * scale), ImVec2(mid.x, mid.y + 6 * scale), col, 1.2f * scale);
    }
    if (s.drag == D::Box) {
        const ImVec2 a = s.pressAt;
        dl->AddRectFilled(ImVec2(std::min(a.x, mouse.x), std::min(a.y, mouse.y)), ImVec2(std::max(a.x, mouse.x), std::max(a.y, mouse.y)),
                          IM_COL32(240, 136, 46, 30));
        dl->AddRect(ImVec2(std::min(a.x, mouse.x), std::min(a.y, mouse.y)), ImVec2(std::max(a.x, mouse.x), std::max(a.y, mouse.y)),
                    IM_COL32(240, 136, 46, 220), 0, 0, 1.0f * scale);
    }
    if (!s.hasUvs) {
        const char* msg = m.triangleCount() ? "No UVs yet: pick a method and Unwrap (or mark seams first)" : "No mesh";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        const ImVec2 at(viewMin.x + (viewSize.x - ts.x) * 0.5f, viewMin.y + viewSize.y * 0.5f - ts.y);
        dl->AddRectFilled(ImVec2(at.x - 12 * scale, at.y - 8 * scale), ImVec2(at.x + ts.x + 12 * scale, at.y + ts.y + 8 * scale),
                          IM_COL32(24, 24, 27, 235), 6 * scale);
        dl->AddText(at, IM_COL32(215, 215, 220, 255), msg);
    }
    if (s.job) {
        const float f = s.job->progress->load();
        const ImVec2 a(viewMin.x + viewSize.x * 0.25f, viewMin.y + viewSize.y * 0.5f), b(viewMin.x + viewSize.x * 0.75f, a.y + 8 * scale);
        dl->AddRectFilled(a, b, IM_COL32(40, 40, 44, 230), 4 * scale);
        dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * f, b.y), theme::u32(theme::accent), 4 * scale);
        dl->AddText(ImVec2(a.x, a.y - ImGui::GetTextLineHeight() - 4 * scale), IM_COL32(220, 220, 225, 255), "Unwrapping...");
    }
    dl->PopClipRect();
    dl->AddRect(viewMin, viewMax, IM_COL32(60, 60, 66, 255));
    ui::HelpText("UV view", nullptr,
                 "Click to select (Shift adds, Ctrl removes), drag a box; drag the box handles to move, scale (corners) or "
                 "rotate (the knob). Keys over the view: 1-4 modes, A all, Alt+A none, Ctrl+I invert, G / R / S move, rotate, "
                 "scale (X / Y lock an axis, Ctrl snaps), F frame, P / Alt+P pin, W weld, Y split. Wheel zooms, right or "
                 "middle drag pans.",
                 kPath);

    // Status line under the view.
    {
        char line[256];
        const UvSelection& sel = doc_.uvSelection;
        const size_t nsel = sel.valid(m) ? sel.count() : 0;
        std::string tiles;
        for (size_t i = 0; i < s.tileList.size() && i < 6; ++i) tiles += (i ? ", " : "") + std::to_string(s.tileList[i]);
        if (s.tileList.size() > 6) tiles += "...";
        if (s.hasUvs)
            std::snprintf(line, sizeof line, "%u islands  |  %zu selected  |  stretch %.3f%s  |  %.0f tx/u at %dK  |  %s",
                          s.topo.islandCount, nsel, double(s.meanStretch), s.flipped > 0.001f ? " (folds)" : "",
                          double(s.meanDensity), u.resolution() / 1024, tiles.c_str());
        else
            std::snprintf(line, sizeof line, "%zu seam edges marked", uvSeamCount(m));
        ImGui::TextDisabled("%s", line);
    }

    // Sidebar.
    ImGui::SetCursorScreenPos(ImVec2(viewMax.x + style.ItemSpacing.x, viewMin.y));
    ImGui::BeginChild("##uvside", ImVec2(sideW, avail.y), false);
    const float w = ImGui::GetContentRegionAvail().x;
    const float halfW = (w - style.ItemSpacing.x) * 0.5f;
    auto section = [&](const char* title, bool open) { return ImGui::CollapsingHeader(title, open ? ImGuiTreeNodeFlags_DefaultOpen : 0); };
    if (section("Unwrap", true)) {
        const char* labels[6];
        for (int i = 0; i < 6; ++i) labels[i] = uvUnwrapMethodLabel(kMethods[i]);
        ImGui::SetNextItemWidth(w);
        ImGui::Combo("##uvmethod", &s.method, labels, 6);
        ui::HelpText("Method", nullptr,
                     "Least Stretch: angles and lengths, along the marked seams (the default). Conformal (LSCM): angles only. "
                     "By Face Groups: also cuts along the face group borders. Planar: each piece projected flat. Unfold Islands: "
                     "the current islands flattened again where they are, keeping pins. Auto Seams: finds its own seams.",
                     kPath);
        if (kMethods[std::clamp(s.method, 0, 5)] == UvUnwrapMethod::Isometric || kMethods[s.method] == UvUnwrapMethod::Polygroups ||
            kMethods[s.method] == UvUnwrapMethod::Unfold) {
            float it = float(s.iterations);
            ImGui::SetNextItemWidth(w);
            if (ui::SliderInside("##uviter", &it, 0.0f, 40.0f, "Refine %.0f")) s.iterations = int(it);
            ui::HelpText("Refine", nullptr, "Least-stretch steps after the conformal start: more keeps lengths better (0 = conformal).", kPath);
        }
        const bool sel = doc_.uvSelection.valid(m) && !doc_.uvSelection.empty() && s.hasUvs;
        ImGui::PushStyleColor(ImGuiCol_Button, theme::accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::accentHover);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::textOnAccent);
        ImGui::BeginDisabled(s.job != nullptr || m.triangleCount() == 0);
        if (ImGui::Button(sel ? "Unwrap Selected##uvunwrap" : "Unwrap##uvunwrap", ImVec2(w, 0))) u.unwrap();
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        ui::HelpAction(*this, "uv.unwrap");
        ImGui::Spacing();
        ImGui::TextDisabled("Seams (%zu edges)", uvSeamCount(m));
        ui::ActionButton(*this, "uv.tool_seams", ImVec2(halfW, 0), ui::Look::IconLabel);
        ImGui::SameLine();
        ui::ActionButton(*this, "uv.tool_select", ImVec2(-1, 0), ui::Look::IconLabel);
        ui::ActionButton(*this, "uv.seam_mark", ImVec2(halfW, 0));
        ImGui::SameLine();
        ui::ActionButton(*this, "uv.seam_clear", ImVec2(-1, 0));
        ui::ActionButton(*this, "uv.seam_polygroups", ImVec2(halfW, 0));
        ImGui::SameLine();
        ui::ActionButton(*this, "uv.seam_islands", ImVec2(-1, 0));
        ImGui::SetNextItemWidth(halfW);
        ui::SliderInside("##uvangle", &s.seamAngle, 10.0f, 150.0f, "%.0f deg");
        ui::HelpText("Sharp angle", nullptr, "Sharp Edges marks the edges whose faces meet at more than this angle.", kPath);
        ImGui::SameLine();
        ui::ActionButton(*this, "uv.seam_angle", ImVec2(-1, 0));
        ui::ActionButton(*this, "uv.seam_clear_all", ImVec2(w, 0));
    }
    if (section("Pack", true)) {
        ImGui::SetNextItemWidth(w);
        ui::SliderInside("##uvmargin", &s.marginPx, 0.0f, 64.0f, "Margin %.0f px");
        ui::HelpText("Margin", nullptr, "Texels kept free between islands (half of it at the tile borders), at the texture's size.", kPath);
        float tiles = float(s.packTiles);
        ImGui::SetNextItemWidth(w);
        if (ui::SliderInside("##uvtiles", &tiles, 1.0f, 10.0f, "UDIM Tiles %.0f")) s.packTiles = int(std::round(tiles));
        ui::HelpText("UDIM tiles", nullptr, "Pack across this many tiles (1001 onwards, along u): more tiles, more texels per island.", kPath);
        ui::Checkbox("Rotate##uvrot", &s.rotate);
        ui::HelpText("Rotate", nullptr, "Turn islands to their smallest box and allow quarter turns, so more fit.", kPath);
        ImGui::SameLine();
        ui::Checkbox("Equal density##uveq", &s.equalDensity);
        ui::HelpText("Equal density", nullptr, "Scale the islands to one texel density before packing (off: they keep their sizes relative to each other).", kPath);
        ui::ActionButton(*this, "uv.pack", ImVec2(w, 0), ui::Look::IconLabel);
    }
    if (section("Texel Density", true)) {
        ImGui::SetNextItemWidth(halfW);
        if (m.texture) {
            ImGui::BeginDisabled();
            int texChoice = 1;
            for (int i = 0; i < 4; ++i)
                if (kSizes[i] == u.resolution()) texChoice = i;
            ImGui::Combo("##uvres", &texChoice, kSizeLabels, 4);
            ImGui::EndDisabled();
        } else if (ImGui::Combo("##uvres", &s.sizeChoice, kSizeLabels, 4)) {
            s.dirty = true;
        }
        ui::HelpText("Texture size", nullptr, "The texture size densities and margins are measured at (the Part's texture's when it has one).", kPath);
        ImGui::SameLine();
        ImGui::TextDisabled("%s %.0f", s.selIslands ? "Selected" : "Mean", double(s.selDensity));
        ImGui::SetNextItemWidth(halfW);
        ImGui::InputFloat("##uvtarget", &s.targetDensity, 0.0f, 0.0f, "%.0f tx/u");
        s.targetDensity = std::clamp(s.targetDensity, 1.0f, 65536.0f);
        ui::HelpText("Target density", nullptr, "Texels per unit of surface length that Set and Pack at Density give the islands.", kPath);
        ImGui::SameLine();
        ui::ActionButton(*this, "uv.density_pick", ImVec2(-1, 0));
        ui::ActionButton(*this, "uv.density_set", ImVec2(halfW, 0));
        ImGui::SameLine();
        ui::ActionButton(*this, "uv.density_match", ImVec2(-1, 0));
        ui::ActionButton(*this, "uv.density_pack", ImVec2(w, 0));
    }
    const bool trimOpen = section("Trim Sheet", false);
    s.trimOpen = trimOpen;
    if (trimOpen) {
        ImGui::SetNextItemWidth(w);
        ImGui::InputText("##uvstrips", s.strips, sizeof s.strips);
        ui::HelpText("Strips", nullptr,
                     "The sheet's horizontal strips: a count (4 equal strips), heights from the top (0.5,0.25,0.25), or v "
                     "ranges (0-0.5;0.5-1). They show over tile 1001.",
                     kPath);
        std::vector<TrimStrip> strips;
        std::string err;
        if (parseTrimStrips(s.strips, strips, err)) {
            s.strip = std::clamp(s.strip, 0, int(strips.size()) - 1);
            for (size_t i = 0; i < strips.size(); ++i) {
                char label[64];
                std::snprintf(label, sizeof label, "%s  (%.2f-%.2f)##uvstrip%zu", strips[i].name.c_str(), double(strips[i].v0),
                              double(strips[i].v1), i);
                if (ImGui::RadioButton(label, s.strip == int(i))) s.strip = int(i);
            }
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.3f, 1.0f), "%s", err.c_str());
        }
        ui::Checkbox("Fit width##uvfit", &s.fitWidth);
        ui::HelpText("Fit width", nullptr, "Squeeze or stretch the row to fill u 0-1 exactly; off keeps the islands' proportions (trims repeat in u).", kPath);
        ui::ActionButton(*this, "uv.trim", ImVec2(w, 0), ui::Look::IconLabel);
    }
    if (section("Edit", true)) {
        ui::ActionButton(*this, "uv.pin", ImVec2(halfW, 0), ui::Look::IconLabel);
        ImGui::SameLine();
        ui::ActionButton(*this, "uv.unpin", ImVec2(-1, 0), ui::Look::IconLabel);
        ui::ActionButton(*this, "uv.weld", ImVec2(halfW, 0), ui::Look::IconLabel);
        ImGui::SameLine();
        ui::ActionButton(*this, "uv.split", ImVec2(-1, 0), ui::Look::IconLabel);
        ui::ActionButton(*this, "uv.straighten", ImVec2(halfW, 0), ui::Look::IconLabel);
        ImGui::SameLine();
        ui::ActionButton(*this, "uv.rotate_90", ImVec2(-1, 0), ui::Look::IconLabel);
        ui::ActionButton(*this, "uv.flip_u", ImVec2(halfW, 0), ui::Look::IconLabel);
        ImGui::SameLine();
        ui::ActionButton(*this, "uv.flip_v", ImVec2(-1, 0), ui::Look::IconLabel);
    }
    if (section("Display", false)) {
        if (s.image == 1) {
            ImGui::SetNextItemWidth(w);
            if (ui::SliderInside("##uvcheckscale", &s.checkerScale, 2.0f, 32.0f, "Checker %.0f per tile")) s.colorsDirty = true;
        }
        if (s.image != 0) {
            ImGui::SetNextItemWidth(w);
            ui::SliderInside("##uvimgop", &s.imageOpacity, 0.0f, 1.0f, "Image %.2f");
        }
    }
    ImGui::Spacing();
    ui::Checkbox("Paint stays on the model##uvkeep", &s.keepTexture);
    ui::HelpText("Paint stays on the model", nullptr,
                 "When the Part has a painted texture, every UV edit moves the paint with it (re-projected onto the new "
                 "layout in the same undo step), so the model looks the same. Off: the texture stays put and the model "
                 "shows the UVs' new place on it (what trim sheets want).",
                 kPath);
    ImGui::EndChild();
    s.frameMs = double(SDL_GetTicksNS() - frameStart) * 1e-6;
    ImGui::End();
}

bool App::uvEditorKey(SDL_Keycode key, SDL_Keymod mod) {
    if (!showUvEditor_ || !uvEditor_) return false;
    auto runAction = [this](const char* id) {
        const Action* a = actions_.find(id);
        if (a && a->run && (!a->enabled || a->enabled())) a->run();
    };
    UvEditorState& s = *uvEditor_;
    UvEditorUi u = uvUi(*this, uvEditor_);
    using D = UvEditorState::Drag;
    if (s.modal) {
        if (key == SDLK_ESCAPE) u.endTransform(true);
        else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) u.endTransform(false);
        else if (key == SDLK_X) s.axis = s.axis == 1 ? 0 : 1;
        else if (key == SDLK_Y) s.axis = s.axis == 2 ? 0 : 2;
        return true;
    }
    if (!s.hovered || s.drag != D::None) return false;
    const bool ctrl = ctrlKey(mod), alt = (mod & SDL_KMOD_ALT) != 0, shift = (mod & SDL_KMOD_SHIFT) != 0;
    if (key >= SDLK_1 && key <= SDLK_4 && !ctrl && !alt && !shift) {
        runCommand("uv_select", {{"op", std::string("mode")}, {"mode", std::string(uvSelectModeId(UvSelectMode(key - SDLK_1)))}});
        s.dirty = true;
        return true;
    }
    if (key == SDLK_A && !ctrl && !shift) {
        runCommand("uv_select", {{"op", std::string(alt ? "none" : "all")}});
        s.dirty = true;
        return true;
    }
    if (key == SDLK_I && ctrl) {
        runCommand("uv_select", {{"op", std::string("invert")}});
        s.dirty = true;
        return true;
    }
    if ((key == SDLK_G || key == SDLK_R || key == SDLK_S) && !ctrl && !alt && !shift) {
        if (!u.beginTransform(key == SDLK_G ? D::Move : key == SDLK_R ? D::Rotate : D::Scale, ImGui::GetIO().MousePos, true))
            status_ = "Select some UVs first";
        return true;
    }
    if (key == SDLK_F && !ctrl && !alt) {
        u.frameAll();
        return true;
    }
    if (key == SDLK_HOME) {
        u.frame(glm::vec2(0.0f), glm::vec2(1.0f));
        return true;
    }
    if (key == SDLK_P && !ctrl) {
        runAction(alt ? "uv.unpin" : "uv.pin");
        return true;
    }
    if (key == SDLK_W && !ctrl && !alt) {
        runAction("uv.weld");
        return true;
    }
    if (key == SDLK_Y && !ctrl && !alt) {
        runAction("uv.split");
        return true;
    }
    return false;
}

bool App::uvToolActive() const { return showUvEditor_ && uvEditor_ && uvEditor_->tool != 0; }

bool App::uvToolMouseDown(float x, float y, SDL_Keymod mod) {
    if (!uvToolActive() || ctrlKey(mod) || previewLod_ >= 0) return false;
    UvEditorState& s = *uvEditor_;
    UvEditorUi u = uvUi(*this, uvEditor_);
    RayHit hit;
    if (!pick(x, y, hit)) return false; // the background navigates
    u.refresh();
    if (s.tool == 1) {
        s.toolClearing = (mod & SDL_KMOD_ALT) != 0;
        s.toolVertex = u.nearestVertex(hit);
        u.twins();
        s.toolStep = std::make_unique<UvEditStep>(doc_, s.toolClearing ? "Clear Seams" : "Mark Seams", false);
    } else {
        s.toolAdding = !(mod & SDL_KMOD_ALT);
        const bool add = (mod & SDL_KMOD_SHIFT) != 0;
        if (!add && s.toolAdding) doc_.uvSelection.clear();
        u.toolSelectAt(hit, !add);
    }
    drag_ = Drag::UvTool;
    return true;
}

void App::uvToolDragTo(float x, float y) {
    if (!uvEditor_) return;
    UvEditorState& s = *uvEditor_;
    UvEditorUi u = uvUi(*this, uvEditor_);
    RayHit hit;
    if (!pick(x, y, hit)) return;
    if (s.tool == 1) {
        const uint32_t v = u.nearestVertex(hit);
        if (v == s.toolVertex || s.toolVertex == ~0u) return;
        const std::vector<uint32_t> path = uvEdgePath(doc_.mesh, s.toolVertex, v);
        uvSetSeams(doc_.mesh, u.twins(), path, !s.toolClearing);
        s.toolVertex = v;
        s.seamKey = ~uint64_t(0);
        s.colorsDirty = true;
    } else {
        u.toolSelectAt(hit, false);
        u.rebuildColors();
    }
}

void App::uvToolRelease() {
    if (!uvEditor_) return;
    UvEditorState& s = *uvEditor_;
    if (s.toolStep) {
        s.toolStep->commit();
        s.toolStep.reset();
        status_ = std::to_string(uvSeamCount(doc_.mesh)) + " seam edges marked (Unwrap cuts along them)";
    }
    s.toolVertex = ~0u;
    s.dirty = true;
}

void App::drawUvEditorOverlay() {
    if (!showUvEditor_ || !uvEditor_ || !doc_.activeSubtoolInfo().visible) return;
    UvEditorState& s = *uvEditor_;
    const Mesh& m = doc_.mesh;
    const size_t nc = m.triangleCount() * 3;
    // The marked seams, in red on the model (the side facing the camera).
    if (m.uvFlags.size() == nc) {
        const uint64_t key = (uint64_t(uintptr_t(m.uvFlags.data())) << 4) ^ doc_.undo.revision() ^ (uint64_t(nc) << 40) ^
                             (s.toolStep ? uint64_t(SDL_GetTicksNS()) : 0);
        if (key != s.seamKey) {
            s.seamCorners.clear();
            const std::vector<uint32_t>& twin = UvEditorUi{*this, s}.twins();
            for (uint32_t c = 0; c < nc; ++c)
                if ((m.uvFlags[c] & kUvSeam) && (twin[c] == kNoCorner || c < twin[c])) s.seamCorners.push_back(c);
            s.seamKey = key;
        }
        const glm::vec3 eye = camera_.position();
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const CanvasRect cr = canvasRect();
        dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
        const size_t step = std::max<size_t>(1, s.seamCorners.size() / 150000);
        const std::vector<uint32_t>& twin = s.twin;
        auto faces = [&](uint32_t t, const glm::vec3& p) {
            const glm::uvec3 tri = m.triangles[t];
            const glm::vec3 n = glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x]);
            return glm::dot(n, eye - p) > 0.0f;
        };
        for (size_t i = 0; i < s.seamCorners.size(); i += step) {
            const uint32_t c = s.seamCorners[i];
            const glm::vec3 a = m.positions[m.triangles[c / 3][c % 3]], b = m.positions[m.triangles[c / 3][(c % 3 + 1) % 3]];
            const glm::vec3 mid = (a + b) * 0.5f;
            const bool front = faces(c / 3, mid) || (twin.size() == nc && twin[c] != kNoCorner && faces(twin[c] / 3, mid));
            if (!front) continue;
            ImVec2 pa, pb;
            if (!gizmoProject(a, pa) || !gizmoProject(b, pb)) continue;
            dl->AddLine(pa, pb, IM_COL32(245, 78, 64, 225), 1.6f * uiScale_);
        }
        dl->PopClipRect();
    }
    // The Seam tool's cursor: the vertex a press would start from.
    if (s.tool != 0 && !ImGui::GetIO().WantCaptureMouse) {
        RayHit hit;
        const ImVec2 mouse(mouseX_, mouseY_);
        if (canvasRect().contains(mouse.x, mouse.y) && pick(mouse.x, mouse.y, hit)) {
            ImVec2 p;
            const glm::vec3 at = s.tool == 1 ? m.positions[UvEditorUi{*this, s}.nearestVertex(hit)] : hit.position;
            if (gizmoProject(at, p)) {
                ImDrawList* fg = ImGui::GetForegroundDrawList();
                const ImU32 col = s.tool == 1 ? ((inputMods() & SDL_KMOD_ALT) ? IM_COL32(150, 200, 255, 255) : IM_COL32(245, 78, 64, 255))
                                              : theme::u32(theme::accent);
                fg->AddCircle(p, 7.0f * uiScale_, col, 24, 2.0f * uiScale_);
                fg->AddCircleFilled(p, 2.5f * uiScale_, col);
            }
        }
    }
}

double App::uvEditorTestValue(const std::string& name) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (name.rfind("uv_", 0) != 0) return nan;
    if (name == "uv_editor_open") return showUvEditor_ ? 1.0 : 0.0;
    const Mesh& m = doc_.mesh;
    if (name == "uv_seams") return double(uvSeamCount(m));
    if (name == "uv_selected") return doc_.uvSelection.valid(m) ? double(doc_.uvSelection.count()) : 0.0;
    if (name == "uv_select_mode") return double(int(doc_.uvSelection.mode));
    if (name == "uv_unwrapping") return uvEditor_ && uvEditor_->job ? 1.0 : 0.0;
    if (name == "uv_unwraps") return uvEditor_ ? double(uvEditor_->unwraps) : 0.0;
    if (name == "uv_frame_ms") return uvEditor_ ? uvEditor_->frameMs : 0.0;
    if (name == "uv_tool") return uvEditor_ ? double(uvEditor_->tool) : 0.0;
    if (name == "uv_view_triangles") return uvEditor_ ? double(uvEditor_->gpu.triangles()) : 0.0;
    if (name == "uv_overlay") return uvEditorOverlay(doc_.activeSubtoolInfo().id) && !uvEditor_->overlay.colors.empty() ? 1.0 : 0.0;
    if (name == "uv_pins") {
        double n = 0;
        for (uint8_t f : m.uvFlags) n += (f & kUvPin) != 0;
        return n;
    }
    if (!m.hasUvs())
        return name == "uv_islands" || name == "uv_tiles" || name == "uv_stretch_x1000" || name == "uv_flipped_x1000" || name == "uv_density" || name == "uv_max_u" ? 0.0 : nan;
    const UvTopology topo = buildUvTopology(m);
    if (name == "uv_islands") return double(topo.islandCount);
    if (name == "uv_stretch_x1000") return double(uvStretchStats(m).mean) * 1000.0;
    if (name == "uv_flipped_x1000") return double(uvStretchStats(m).flipped) * 1000.0;
    if (name == "uv_density") return double(uvMeshDensity(m, m.texture ? std::max(m.texture->width, m.texture->height) : 2048));
    if (name == "uv_tiles") {
        std::vector<int> tiles;
        for (uint32_t t = 0; t < m.triangleCount(); ++t) tiles.push_back(udimTile((m.uvs[t * 3] + m.uvs[t * 3 + 1] + m.uvs[t * 3 + 2]) / 3.0f));
        std::sort(tiles.begin(), tiles.end());
        return double(std::unique(tiles.begin(), tiles.end()) - tiles.begin());
    }
    if (name == "uv_max_u") {
        float hi = 0.0f;
        for (const glm::vec2& u : m.uvs) hi = std::max(hi, u.x);
        return double(hi);
    }
    return nan;
}

} // namespace sf
