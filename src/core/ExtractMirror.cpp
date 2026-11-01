// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ExtractMirror.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/PaintLayer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace sf {

namespace {

constexpr uint32_t kNone = ~0u;

float largestSide(const Mesh& mesh) {
    const AABB b = mesh.bounds();
    if (!b.valid()) return 1.0f;
    const glm::vec3 e = b.extent();
    const float s = std::max({e.x, e.y, e.z});
    return s > 0.0f ? s : 1.0f;
}

// The directed edges (a, b) of `tris` that no other triangle shares: the open boundary, wound as in the triangles.
std::vector<std::pair<uint32_t, uint32_t>> boundaryEdges(const std::vector<glm::uvec3>& tris) {
    std::vector<std::pair<uint64_t, uint32_t>> edges; // undirected key, directed edge index (3 * t + k)
    edges.reserve(tris.size() * 3);
    for (uint32_t t = 0; t < tris.size(); ++t)
        for (uint32_t k = 0; k < 3; ++k) {
            const uint32_t a = tris[t][k], b = tris[t][(k + 1) % 3];
            edges.push_back({(uint64_t(std::min(a, b)) << 32) | std::max(a, b), 3 * t + k});
        }
    std::sort(edges.begin(), edges.end());
    std::vector<std::pair<uint32_t, uint32_t>> out;
    for (size_t i = 0; i < edges.size();) {
        size_t j = i + 1;
        while (j < edges.size() && edges[j].first == edges[i].first) ++j;
        if (j - i == 1) {
            const uint32_t t = edges[i].second / 3, k = edges[i].second % 3;
            out.push_back({tris[t][k], tris[t][(k + 1) % 3]});
        }
        i = j;
    }
    return out;
}

int axisIndex(char c) { return c == 'x' ? 0 : c == 'y' ? 1 : c == 'z' ? 2 : -1; }
const char* axisName(int axis) { return axis == 0 ? "X" : axis == 1 ? "Y" : "Z"; }

// "x", "xz", "X, Y": the axes named, in order. False on anything else.
bool parseAxes(const std::string& text, std::vector<int>& axes) {
    for (char c : text) {
        const char l = char(std::tolower(static_cast<unsigned char>(c)));
        if (l == ',' || l == ' ') continue;
        const int a = axisIndex(l);
        if (a < 0) return false;
        if (std::find(axes.begin(), axes.end(), a) == axes.end()) axes.push_back(a);
    }
    return !axes.empty();
}

} // namespace

Mesh extractShell(const Mesh& mesh, const ExtractOptions& o, std::string& error) {
    const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
    if (!mesh.zspheres.empty()) {
        error = "Extract works on meshes; make the Forge Spheres a mesh first";
        return {};
    }
    // The patch: the visible triangles whose corners are masked (on average), with their own vertices.
    std::vector<uint32_t> remap(nv, kNone), kept, patchOf(mesh.hasQuads() ? nt : 0, kNone);
    std::vector<glm::uvec3> tris;
    if (mesh.hasMask())
        for (uint32_t t = 0; t < nt; ++t) {
            const glm::uvec3 c = mesh.triangles[t];
            if (mesh.triangleHidden(t) || (mesh.mask[c.x] + mesh.mask[c.y] + mesh.mask[c.z]) < 3.0f * o.threshold) continue;
            glm::uvec3 nt3;
            for (int k = 0; k < 3; ++k) {
                uint32_t& r = remap[c[k]];
                if (r == kNone) {
                    r = uint32_t(kept.size());
                    kept.push_back(c[k]);
                }
                nt3[k] = r;
            }
            if (!patchOf.empty()) patchOf[t] = uint32_t(tris.size());
            tris.push_back(nt3);
        }
    if (tris.empty()) {
        error = "Nothing to extract: mask the part first (Ctrl+drag on the surface)";
        return {};
    }
    const uint32_t np = uint32_t(kept.size());
    std::vector<glm::vec3> p(np);
    for (uint32_t i = 0; i < np; ++i) p[i] = mesh.positions[kept[i]];

    // E Smt: Taubin smoothing along the patch's edge, which follows the triangles' staircase, so it doesn't shrink.
    const auto boundary = boundaryEdges(tris);
    if (o.smooth > 0 && !boundary.empty()) {
        std::vector<std::array<uint32_t, 2>> nb(np, {kNone, kNone});
        std::vector<uint8_t> count(np, 0);
        for (const auto& [a, b] : boundary)
            for (const auto& [v, w] : {std::pair{a, b}, std::pair{b, a}}) {
                if (count[v] < 2) nb[v][count[v]] = w;
                count[v] = uint8_t(std::min(3, count[v] + 1));
            }
        std::vector<uint32_t> chain; // vertices on one simple boundary loop (pinch points stay put)
        for (uint32_t v = 0; v < np; ++v)
            if (count[v] == 2) chain.push_back(v);
        std::vector<glm::vec3> moved(chain.size());
        for (int it = 0; it < 4 * std::min(o.smooth, 50); ++it) // 4 passes a step: the steps span a few edges
            for (float f : {0.5f, -0.53f}) {
                for (size_t i = 0; i < chain.size(); ++i) {
                    const uint32_t v = chain[i];
                    const glm::vec3 mid = (p[nb[v][0]] + p[nb[v][1]]) * 0.5f;
                    moved[i] = p[v] + f * (mid - p[v]);
                }
                for (size_t i = 0; i < chain.size(); ++i) p[chain[i]] = moved[i];
            }
    }

    Mesh patch;
    patch.positions = p;
    patch.triangles = tris;
    patch.rebuildTopology();
    patch.recomputeAllNormals();

    const float t = o.thickness * largestSide(mesh);
    const float outer = o.doubleSided ? std::abs(t) : std::max(t, 0.0f);
    const float inner = o.doubleSided ? -std::abs(t) : std::min(t, 0.0f);
    const bool single = outer == inner;
    const uint32_t layers = single ? 1 : 2;
    const uint32_t ntp = uint32_t(tris.size());

    Mesh out;
    out.positions.reserve(size_t(np) * layers);
    for (uint32_t i = 0; i < np; ++i) out.positions.push_back(p[i] + patch.normals[i] * outer);
    if (!single)
        for (uint32_t i = 0; i < np; ++i) out.positions.push_back(p[i] + patch.normals[i] * inner);
    if (mesh.hasColors()) {
        out.colors.reserve(out.positions.size());
        for (uint32_t l = 0; l < layers; ++l)
            for (uint32_t v : kept) out.colors.push_back(mesh.colors[v]);
    }
    out.triangles = tris;
    out.polygroups.assign(tris.size(), 1u);
    if (!single) {
        // The inner surface faces the other way; the rim joins the two along the patch's edge, one quad per edge.
        for (const glm::uvec3& c : tris) out.triangles.push_back(glm::uvec3(c.x + np, c.z + np, c.y + np));
        out.polygroups.resize(out.triangles.size(), 2u);
        // TCorne: where the edge turns sharply (over 50 degrees) the rim keeps triangles; TBorder: all along it.
        std::vector<uint8_t> corner(o.triangleCorners && !o.triangleBorder ? np : 0, 0);
        if (!corner.empty()) {
            std::vector<uint32_t> next(np, kNone), prev(np, kNone);
            std::vector<uint8_t> uses(np, 0);
            for (const auto& [u, v] : boundary) {
                next[u] = v;
                prev[v] = u;
                ++uses[u];
            }
            const float cosLimit = std::cos(glm::radians(50.0f));
            for (uint32_t v = 0; v < np; ++v) {
                if (next[v] == kNone || prev[v] == kNone) continue;
                const glm::vec3 a = p[v] - p[prev[v]], b = p[next[v]] - p[v];
                const float la = glm::length(a), lb = glm::length(b);
                corner[v] = uses[v] > 1 || (la > 0.0f && lb > 0.0f && glm::dot(a, b) < cosLimit * la * lb);
            }
        }
        for (const auto& [u, v] : boundary) {
            const uint32_t first = uint32_t(out.triangles.size());
            out.triangles.push_back(glm::uvec3(v, u, u + np));
            out.triangles.push_back(glm::uvec3(v, u + np, v + np));
            out.polygroups.push_back(3u);
            out.polygroups.push_back(3u);
            if (!o.triangleBorder && (corner.empty() || (!corner[u] && !corner[v]))) out.pairQuad(first, first + 1);
        }
    }
    // Quads of the source stay quads on both surfaces.
    for (uint32_t t0 = 0; t0 < patchOf.size(); ++t0) {
        const uint32_t u0 = mesh.quadPartnerAt(t0);
        if (patchOf[t0] == kNone || u0 == Mesh::kNoQuad || u0 < t0 || patchOf[u0] == kNone) continue;
        for (uint32_t l = 0; l < layers; ++l) out.pairQuad(patchOf[t0] + l * ntp, patchOf[u0] + l * ntp);
    }
    if (out.hasQuads()) out.quadPartners.resize(out.triangles.size(), Mesh::kNoQuad);
    out.rebuildTopology();
    out.recomputeAllNormals();
    return out;
}

Mesh mirrorAndWeld(const Mesh& mesh, int axis, bool keepPositive, std::string& error) {
    const size_t nv = mesh.vertexCount(), nt = mesh.triangleCount();
    const float sign = keepPositive ? 1.0f : -1.0f;
    const float size = largestSide(mesh);
    // Signed distance to the plane on the kept side. Vertices on it (within a hair), and open edges' vertices
    // close to it (a half model whose seam isn't quite at 0), are welded onto it.
    std::vector<float> d(nv);
    for (size_t v = 0; v < nv; ++v) {
        d[v] = sign * mesh.positions[v][axis];
        if (std::abs(d[v]) < 1e-5f * size) d[v] = 0.0f;
    }
    // (Only vertices near the plane are checked for an open edge, through the mesh's adjacency.)
    for (uint32_t v = 0; v < nv; ++v) {
        if (d[v] == 0.0f || std::abs(d[v]) >= 1e-3f * size) continue;
        const IndexRing around = mesh.vertexTriangles(v);
        for (uint32_t w : mesh.neighbors(v)) {
            int shared = 0;
            for (uint32_t t : around) {
                const glm::uvec3 c = mesh.triangles[t];
                shared += c.x == w || c.y == w || c.z == w;
            }
            if (shared == 1) {
                d[v] = 0.0f;
                break;
            }
        }
    }

    // The kept half: whole triangles on the kept side, and those crossing the plane cut on it.
    struct Blend {
        uint32_t a, b;
        float t;
    };
    std::vector<Blend> verts;
    std::vector<uint32_t> newOf(nv, kNone);
    std::unordered_map<uint64_t, uint32_t> cuts;
    auto keepVertex = [&](uint32_t v) {
        if (newOf[v] == kNone) {
            newOf[v] = uint32_t(verts.size());
            verts.push_back({v, v, 0.0f});
        }
        return newOf[v];
    };
    auto cutVertex = [&](uint32_t a, uint32_t b) {
        const uint64_t key = (uint64_t(std::min(a, b)) << 32) | std::max(a, b);
        auto [it, added] = cuts.emplace(key, uint32_t(verts.size()));
        if (added) verts.push_back({a, b, d[a] / (d[a] - d[b])});
        return it->second;
    };
    struct Tri {
        glm::uvec3 v;
        uint32_t source;
        std::array<glm::vec2, 3> uv;
    };
    std::vector<Tri> half;
    std::vector<uint32_t> intact(mesh.hasQuads() ? nt : 0, kNone); // source triangle -> its uncut copy in `half`
    const bool uvs = mesh.hasUvs();
    for (uint32_t t = 0; t < nt; ++t) {
        const glm::uvec3 c = mesh.triangles[t];
        const float d0 = d[c.x], d1 = d[c.y], d2 = d[c.z];
        std::array<glm::vec2, 3> uv{};
        if (uvs) uv = {mesh.uvs[3 * t], mesh.uvs[3 * t + 1], mesh.uvs[3 * t + 2]};
        if (d0 >= 0.0f && d1 >= 0.0f && d2 >= 0.0f) {
            if (d0 == 0.0f && d1 == 0.0f && d2 == 0.0f) continue; // in the plane: the mirror would double it
            if (!intact.empty()) intact[t] = uint32_t(half.size());
            half.push_back({glm::uvec3(keepVertex(c.x), keepVertex(c.y), keepVertex(c.z)), t, uv});
            continue;
        }
        if (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f) continue;
        // Clip to the kept side (a triangle or a quad), then fan it into triangles.
        std::array<uint32_t, 4> poly;
        std::array<glm::vec2, 4> polyUv;
        int n = 0;
        for (int i = 0; i < 3; ++i) {
            const int j = (i + 1) % 3;
            const float di = d[c[i]], dj = d[c[j]];
            if (di >= 0.0f) {
                poly[n] = keepVertex(c[i]);
                polyUv[n++] = uv[i];
            }
            if ((di > 0.0f && dj < 0.0f) || (di < 0.0f && dj > 0.0f)) {
                const float f = di / (di - dj);
                poly[n] = cutVertex(c[i], c[j]);
                polyUv[n++] = glm::mix(uv[i], uv[j], f);
            }
        }
        for (int k = 1; k + 1 < n; ++k)
            half.push_back({glm::uvec3(poly[0], poly[k], poly[k + 1]), t, {polyUv[0], polyUv[k], polyUv[k + 1]}});
    }
    if (half.empty()) {
        error = std::string("Nothing on the ") + (keepPositive ? "+" : "-") + axisName(axis) + " side to mirror";
        return {};
    }

    // Vertices on the plane are shared by both halves (the weld); the others get a mirrored twin.
    const uint32_t nh = uint32_t(verts.size());
    std::vector<uint8_t> onPlane(nh);
    std::vector<uint32_t> twin(nh);
    std::vector<uint32_t> sourceOf; // twin index - nh -> half vertex
    for (uint32_t k = 0; k < nh; ++k) {
        onPlane[k] = verts[k].a != verts[k].b || d[verts[k].a] == 0.0f;
        if (onPlane[k]) {
            twin[k] = k;
        } else {
            twin[k] = nh + uint32_t(sourceOf.size());
            sourceOf.push_back(k);
        }
    }
    const size_t total = nh + sourceOf.size();
    auto lerp3 = [&](const std::vector<glm::vec3>& src, const Blend& b) { return glm::mix(src[b.a], src[b.b], b.t); };
    auto flip = [&](glm::vec3 v) {
        v[axis] = -v[axis];
        return v;
    };
    // A vec3 per vertex: positions, the layer base and offsets. On the plane the axis component is exactly 0.
    auto vectorAttribute = [&](const std::vector<glm::vec3>& src, std::vector<glm::vec3>& dst) {
        dst.resize(total);
        for (uint32_t k = 0; k < nh; ++k) {
            dst[k] = lerp3(src, verts[k]);
            if (onPlane[k]) dst[k][axis] = 0.0f;
        }
        for (size_t i = 0; i < sourceOf.size(); ++i) dst[nh + i] = flip(dst[sourceOf[i]]);
    };
    // Anything else per vertex is interpolated and copied to the twin.
    auto scalarAttribute = [&](const auto& src, auto& dst) {
        dst.resize(total);
        for (uint32_t k = 0; k < nh; ++k) dst[k] = src[verts[k].a] + (src[verts[k].b] - src[verts[k].a]) * verts[k].t;
        for (size_t i = 0; i < sourceOf.size(); ++i) dst[nh + i] = dst[sourceOf[i]];
    };

    Mesh out;
    vectorAttribute(mesh.positions, out.positions);
    if (!mesh.layers.empty()) {
        vectorAttribute(mesh.base, out.base);
        for (const SculptLayer& l : mesh.layers) {
            out.layers.push_back({l.name, {}, l.weight});
            vectorAttribute(l.delta, out.layers.back().delta);
        }
    }
    if (mesh.hasMask()) scalarAttribute(mesh.mask, out.mask);
    if (mesh.hasColors()) scalarAttribute(mesh.colors, out.colors);
    if (!mesh.paintLayers.empty() && mesh.paintBase.size() == nv) {
        scalarAttribute(mesh.paintBase, out.paintBase);
        for (const PaintLayer& l : mesh.paintLayers) {
            PaintLayer nl = l;
            scalarAttribute(l.color, nl.color);
            scalarAttribute(l.alpha, nl.alpha);
            out.paintLayers.push_back(std::move(nl));
        }
        out.activePaintLayer = mesh.activePaintLayer;
    }
    if (uvs) { // the texture fits both halves (the mirrored one reuses the UVs)
        out.texture = mesh.texture;
        out.paintToTexture = mesh.paintToTexture;
    }

    std::vector<uint32_t> mirrored(half.size(), kNone);
    auto addTriangle = [&](const glm::uvec3& v, uint32_t source, const std::array<glm::vec2, 3>& uv) {
        out.triangles.push_back(v);
        if (mesh.hasPolygroups()) out.polygroups.push_back(mesh.polygroups[source]);
        if (mesh.hasHidden()) out.hidden.push_back(mesh.hidden[source]);
        if (uvs) out.uvs.insert(out.uvs.end(), uv.begin(), uv.end());
    };
    for (const Tri& t : half) addTriangle(t.v, t.source, t.uv);
    for (size_t i = 0; i < half.size(); ++i) {
        const Tri& t = half[i];
        if (onPlane[t.v.x] && onPlane[t.v.y] && onPlane[t.v.z]) continue;
        mirrored[i] = uint32_t(out.triangles.size());
        addTriangle(glm::uvec3(twin[t.v.x], twin[t.v.z], twin[t.v.y]), t.source, {t.uv[0], t.uv[2], t.uv[1]});
    }
    for (uint32_t t = 0; t < intact.size(); ++t) {
        const uint32_t u = mesh.quadPartners[t];
        if (intact[t] == kNone || u >= nt || u < t || intact[u] == kNone) continue;
        out.pairQuad(intact[t], intact[u]);
        if (mirrored[intact[t]] != kNone && mirrored[intact[u]] != kNone)
            out.pairQuad(mirrored[intact[t]], mirrored[intact[u]]);
    }
    if (out.hasQuads()) out.quadPartners.resize(out.triangles.size(), Mesh::kNoQuad);
    out.rebuildTopology();
    if (!out.layers.empty()) out.evaluateAll();
    if (!out.paintLayers.empty()) compositePaintAll(out);
    out.recomputeAllNormals();
    return out;
}

Mesh mirrorFlip(const Mesh& mesh, int axis, float pivot) {
    Mesh out = mesh;
    out.multires.reset(); // the other levels aren't flipped
    for (auto& p : out.positions) p[axis] = 2.0f * pivot - p[axis];
    for (auto& p : out.base) p[axis] = 2.0f * pivot - p[axis];
    for (auto& l : out.layers)
        for (auto& v : l.delta) v[axis] = -v[axis];
    for (auto& z : out.zspheres) z.center[axis] = 2.0f * pivot - z.center[axis];
    for (auto& t : out.triangles) std::swap(t.y, t.z);
    for (size_t t = 0; t * 3 + 2 < out.uvs.size(); ++t) std::swap(out.uvs[3 * t + 1], out.uvs[3 * t + 2]);
    out.rebuildTopology();
    out.recomputeAllNormals();
    return out;
}

namespace {

using T = ParamSpec::Type;

std::string triangles(size_t n) { return std::to_string(n) + " triangles"; }

CommandResult acceptPreview(Document& doc) {
    if (!doc.extractPreviewCurrent()) {
        doc.dropExtractPreview();
        return {false, "Nothing to accept: press Extract first"};
    }
    const int i = doc.subtoolIndex(doc.extractPreview.id);
    UndoRecord r;
    r.label = "Extract";
    r.subtool = std::make_shared<SubtoolStep>();
    r.subtool->id = doc.extractPreview.id;
    r.subtool->at = i;
    doc.undo.push(std::move(r));
    doc.extractPreview = {};
    doc.modified = true;
    return {true, "Extracted " + doc.subtools[size_t(i)].name + " (" + triangles(doc.subtoolMesh(i).triangleCount()) + ")"};
}

CommandResult runExtract(Document& doc, const Args& a) {
    ExtractOptions o;
    o.thickness = std::clamp(float(argNumber(a, "thickness", o.thickness)), -1.0f, 1.0f);
    o.smooth = std::clamp(int(std::lround(argNumber(a, "smooth", o.smooth))), 0, 50);
    o.doubleSided = argBool(a, "double", false);
    o.triangleCorners = argBool(a, "tcorne", false);
    o.triangleBorder = argBool(a, "tborder", false);
    std::string error;
    Mesh shell = extractShell(doc.mesh, o, error);
    if (shell.triangleCount() == 0) {
        doc.dropExtractPreview();
        return {false, error};
    }
    const size_t tris = shell.triangleCount();
    doc.dropExtractPreview(true);
    if (doc.extractPreview.id) { // Extract again (other settings): the preview is replaced
        SubTool& s = doc.subtools[size_t(doc.subtoolIndex(doc.extractPreview.id))];
        s.mesh = std::move(shell);
        s.mesh.topologyDirty = true;
        s.bvh.build(s.mesh);
        s.bvhDirty = false;
    } else {
        const int i = doc.addSubtool(std::move(shell), "Extract", false, doc.activeSubtool + 1);
        doc.extractPreview = {doc.subtools[size_t(i)].id, doc.activeSubtoolInfo().id, doc.undo.revision()};
    }
    if (argBool(a, "accept", false)) return acceptPreview(doc);
    return {true, "Extract preview (" + triangles(tris) + "): Accept keeps it as a new Part"};
}

} // namespace

void registerExtractMirrorCommands(CommandRegistry& reg) {
    reg.add({"extract",
             "ZBrush Extract (Tool > SubTool): make a shell from the masked, visible part of the active Part and "
             "show it as a preview below it; extract_accept (or accept=true) keeps it as a new Part in one undo "
             "step. Running it again replaces the preview.",
             {{"thickness", T::Number,
               "Thick: shell thickness as a fraction of the Part's largest side (default 0.015; negative grows "
               "inward; 0 = a single-sided surface).",
               false},
              {"smooth", T::Number, "E Smt: smoothing passes along the extracted edge (0-50, default 5).", false},
              {"double", T::Bool, "Double: grow the shell both inward and outward (default false).", false},
              {"tcorne", T::Bool, "TCorne: triangles instead of quads in the rim's sharp corners (default false).", false},
              {"tborder", T::Bool, "TBorder: the whole rim is triangles instead of quads (default false).", false},
              {"accept", T::Bool, "Accept at once instead of leaving a preview (default false).", false}},
             runExtract});
    reg.add({"extract_accept", "Keep the Extract preview as a new Part (undo removes it again).", {},
             [](Document& doc, const Args&) { return acceptPreview(doc); }});
    reg.add({"extract_cancel", "Remove the Extract preview.", {},
             [](Document& doc, const Args&) -> CommandResult {
                 if (!doc.extractPreview.id) return {false, "There's no Extract preview"};
                 doc.dropExtractPreview();
                 return {true, "Extract cancelled"};
             }});

    reg.add({"mirror_and_weld",
             "ZBrush Mirror And Weld (Tool > Geometry > Modify Topology): keep the half of the active Part on the "
             "+ side of the plane through the origin, mirror it onto the other side and weld the seam into one "
             "watertight mesh. Face Groups, vertex paint, mask, UVs and sculpt layers are kept. One undo step.",
             {{"axis", T::String, "Axis whose plane to mirror across: x (default), y or z; several (xz) in turn.", false},
              {"side", T::String, "The side kept: positive (default) or negative.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<int> axes;
                 if (!parseAxes(argString(a, "axis", "x"), axes)) return {false, "Bad axis (expected x, y, z or e.g. xz)"};
                 const std::string side = argString(a, "side", "positive");
                 const bool positive = side == "positive" || side == "+" || side == "pos";
                 if (!positive && side != "negative" && side != "-" && side != "neg")
                     return {false, "Bad side '" + side + "' (positive or negative)"};
                 if (!doc.mesh.zspheres.empty()) return {false, "Mirror And Weld works on meshes; make the Forge Spheres a mesh first"};
                 if (doc.mesh.triangleCount() == 0) return {false, "The Part is empty"};
                 const size_t before = doc.mesh.triangleCount();
                 Mesh m = doc.mesh;
                 std::string names;
                 for (int axis : axes) {
                     std::string error;
                     m = mirrorAndWeld(m, axis, positive, error);
                     if (m.triangleCount() == 0) return {false, error};
                     names += axisName(axis);
                 }
                 const size_t after = m.triangleCount();
                 doc.replaceMeshUndoable(std::move(m), "Mirror And Weld");
                 return {true, "Mirror And Weld " + names + ": " + triangles(before) + " -> " + triangles(after)};
             }});

    reg.add({"mirror",
             "ZBrush Mirror (Tool > Deformation): flip the active Part across a plane, by default the one through "
             "the origin that symmetry and Mirror And Weld use. One undo step.",
             {{"axis", T::String, "Axis to flip along: x (default), y or z; several (xz) in turn.", false},
              {"pivot", T::String, "origin (default) or center (flip in place about the Part's centre).", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<int> axes;
                 if (!parseAxes(argString(a, "axis", "x"), axes)) return {false, "Bad axis (expected x, y, z or e.g. xz)"};
                 const std::string pivot = argString(a, "pivot", "origin");
                 const bool center = pivot == "center" || pivot == "centre";
                 if (!center && pivot != "origin") return {false, "Bad pivot '" + pivot + "' (origin or center)"};
                 if (doc.mesh.vertexCount() == 0) return {false, "The Part is empty"};
                 const glm::vec3 c = doc.mesh.bounds().center();
                 Mesh m = doc.mesh;
                 std::string names;
                 for (int axis : axes) {
                     m = mirrorFlip(m, axis, center ? c[axis] : 0.0f);
                     names += axisName(axis);
                 }
                 doc.replaceMeshUndoable(std::move(m), "Mirror");
                 return {true, "Mirrored across " + names + (center ? " (about its centre)" : "")};
             }});
}

} // namespace sf
