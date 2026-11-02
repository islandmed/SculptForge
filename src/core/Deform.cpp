// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Deform.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cmath>

namespace sf {

const char* deformName(DeformOp op) {
    switch (op) {
    case DeformOp::Polish: return "Polish";
    case DeformOp::Relax: return "Relax";
    case DeformOp::Smooth: return "Smooth";
    case DeformOp::Balloon: return "Balloon";
    }
    return "Deform";
}

namespace {

using glm::vec3;

constexpr size_t kGrain = 2048;

// How much each vertex may move: 0 when hidden, on an open border (or a non-manifold one) or unconnected;
// otherwise 1 - mask (1 with useMask off).
std::vector<float> freedom(const Mesh& m, bool useMask) {
    std::vector<float> k(m.vertexCount(), 0.0f);
    poolFor(k.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const uint32_t v = uint32_t(i);
            const IndexRing ring = m.neighbors(v);
            if (ring.empty() || ring.size() != m.vertexTriangles(v).size()) continue; // a border: n neighbours, n-1 faces
            if (m.hasHidden() && m.vertexHidden(v)) continue;
            k[i] = useMask ? 1.0f - m.maskAt(v) : 1.0f;
        }
    }, kGrain);
    return k;
}

// Area-weighted vertex normals of the surface at positions P.
void normalsOf(const Mesh& m, const std::vector<vec3>& P, std::vector<vec3>& N) {
    N.resize(P.size());
    poolFor(P.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            vec3 n(0.0f);
            for (uint32_t t : m.vertexTriangles(uint32_t(i))) {
                const glm::uvec3& f = m.triangles[t];
                n += glm::cross(P[f.y] - P[f.x], P[f.z] - P[f.x]);
            }
            const float l = glm::length(n);
            N[i] = l > 1e-30f ? n / l : vec3(0.0f);
        }
    }, kGrain);
}

vec3 laplacian(const Mesh& m, const std::vector<vec3>& P, uint32_t v) {
    const IndexRing ring = m.polygonNeighbors(v);
    vec3 avg(0.0f);
    for (uint32_t n : ring) avg += P[n];
    return avg / float(ring.size()) - P[v];
}

// One Jacobi step: Q[v] = P[v] + k[v] * step(v) for the vertices that may move.
template <class F>
void step(const std::vector<float>& k, std::vector<vec3>& P, std::vector<vec3>& Q, F&& delta) {
    Q.resize(P.size());
    poolFor(P.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) Q[i] = k[i] > 0.0f ? P[i] + delta(uint32_t(i)) * k[i] : P[i];
    }, kGrain);
    P.swap(Q);
}

// The amount as passes: `most` passes at 100, each a fraction of a full one so the total is proportional.
void passes(float amount, int most, int& count, float& each) {
    const float t = std::clamp(std::abs(amount), 0.0f, 100.0f) / 100.0f * float(most);
    count = int(std::ceil(t - 1e-4f));
    each = count > 0 ? t / float(count) : 0.0f;
}

} // namespace

std::vector<vec3> deformPositions(const Mesh& m, DeformOp op, const DeformOptions& o) {
    std::vector<vec3> P = m.positions, Q, N;
    if (m.vertexCount() == 0 || m.triangleCount() == 0) return P;
    const std::vector<float> k = freedom(m, o.useMask);
    int count = 0;
    float each = 0.0f;
    switch (op) {
    case DeformOp::Smooth: {
        passes(o.amount, 20, count, each);
        const float lambda = 0.5f * each;
        // Taubin: mu from lambda and the pass-band frequency 0.1 (1/lambda + 1/mu = 0.1), a little stronger back.
        const float mu = 1.0f / (0.1f - 1.0f / std::max(lambda, 1e-3f));
        for (int i = 0; i < count; ++i) {
            step(k, P, Q, [&](uint32_t v) { return laplacian(m, P, v) * lambda; });
            if (o.keepVolume) step(k, P, Q, [&](uint32_t v) { return laplacian(m, P, v) * mu; });
        }
        break;
    }
    case DeformOp::Relax: {
        passes(o.amount, 20, count, each);
        const float lambda = 0.5f * each;
        for (int i = 0; i < count; ++i) {
            normalsOf(m, P, N);
            step(k, P, Q, [&](uint32_t v) {
                const vec3 l = laplacian(m, P, v);
                return (l - N[v] * glm::dot(N[v], l)) * lambda; // along the surface only
            });
        }
        break;
    }
    case DeformOp::Polish: {
        // Bilateral normal filtering (Zheng et al. 2011): each face's normal becomes a weighted average of the
        // normals of the faces around it, weighted down by distance and, steeply, by how different they are, so
        // the small tilts of noise average out while the faces across an edge or corner don't count. Then the
        // vertices move to fit the filtered normals (Sun et al. 2007), which keeps edges where they are.
        passes(o.amount, 6, count, each);
        const size_t nt = m.triangleCount();
        std::vector<vec3> fn(nt), fc(nt), filtered(nt);
        std::vector<float> fa(nt);
        auto faces = [&] {
            poolFor(nt, [&](size_t b, size_t e) {
                for (size_t t = b; t < e; ++t) {
                    const glm::uvec3& f = m.triangles[t];
                    const vec3 c = glm::cross(P[f.y] - P[f.x], P[f.z] - P[f.x]);
                    const float l = glm::length(c);
                    fa[t] = 0.5f * l;
                    fn[t] = l > 1e-30f ? c / l : vec3(0.0f);
                    fc[t] = (P[f.x] + P[f.y] + P[f.z]) / 3.0f;
                }
            }, kGrain);
        };
        constexpr float kSigmaNormal = 0.35f; // |n_f - n_g| of a 20 degree tilt; a 90 degree edge is 1.41
        for (int i = 0; i < count; ++i) {
            faces();
            poolFor(nt, [&](size_t b, size_t e) {
                for (size_t t = b; t < e; ++t) {
                    const glm::uvec3& f = m.triangles[t];
                    // The faces sharing a corner with t (some twice, which only weighs them a little more).
                    float spacing = 0.0f;
                    for (int c = 0; c < 3; ++c) spacing += glm::distance(P[f[c]], fc[t]);
                    spacing = std::max(spacing / 3.0f * 2.0f, 1e-20f);
                    vec3 sum(0.0f);
                    for (int c = 0; c < 3; ++c)
                        for (uint32_t g : m.vertexTriangles(f[c])) {
                            const vec3 dn = fn[g] - fn[t];
                            const vec3 dc = fc[g] - fc[t];
                            const float w = fa[g] * std::exp(-glm::dot(dc, dc) / (2.0f * spacing * spacing)) *
                                            std::exp(-glm::dot(dn, dn) / (2.0f * kSigmaNormal * kSigmaNormal));
                            sum += fn[g] * w;
                        }
                    const float l = glm::length(sum);
                    filtered[t] = l > 1e-30f ? sum / l : fn[t];
                }
            }, kGrain);
            for (int fit = 0; fit < 3; ++fit) {
                if (fit > 0) faces();
                step(k, P, Q, [&](uint32_t v) {
                    vec3 d(0.0f);
                    const IndexRing tris = m.vertexTriangles(v);
                    for (uint32_t t : tris) d += filtered[t] * glm::dot(filtered[t], fc[t] - P[v]);
                    return d * (each / float(tris.size()));
                });
            }
        }
        break;
    }
    case DeformOp::Balloon: {
        passes(o.amount, 10, count, each);
        const float diagonal = glm::length(m.bounds().extent());
        const float push = (o.amount < 0.0f ? -1.0f : 1.0f) * 0.01f * diagonal * each;
        for (int i = 0; i < count; ++i) {
            normalsOf(m, P, N);
            // Out along the normal, then the skin pulls taut (a Laplacian step): flat sides bulge, corners round.
            step(k, P, Q, [&](uint32_t v) { return N[v] * push; });
            step(k, P, Q, [&](uint32_t v) { return laplacian(m, P, v) * (0.5f * each); });
        }
        break;
    }
    }
    return P;
}

size_t deformActivePart(Document& doc, DeformOp op, const DeformOptions& o, std::string& error) {
    Mesh& mesh = doc.mesh;
    const std::string name = deformName(op);
    if (doc.sculptor.strokeActive()) {
        error = name + ": finish the stroke first";
        return 0;
    }
    if (mesh.vertexCount() == 0 || mesh.triangleCount() == 0) {
        error = name + ": the active Part is empty";
        return 0;
    }
    if (!mesh.zspheres.empty()) {
        error = name + ": the active Part is a Forge Sphere Part (Make Mesh first)";
        return 0;
    }
    if (mesh.micro) {
        error = name + ": the active Part has a micro level (Bake Micro or Delete Micro first)";
        return 0;
    }
    doc.syncBvh();
    const std::vector<vec3> P = deformPositions(mesh, op, o);
    std::vector<uint32_t> verts;
    std::vector<vec3> d;
    for (uint32_t v = 0; v < mesh.vertexCount(); ++v) {
        const vec3 dv = P[v] - mesh.positions[v];
        if (dv != vec3(0.0f) && std::isfinite(dv.x + dv.y + dv.z)) {
            verts.push_back(v);
            d.push_back(dv);
        }
    }
    if (verts.empty()) {
        error = name + ": nothing moved" + (o.useMask && mesh.hasMask() ? " (is everything masked?)" : "");
        return 0;
    }
    if (!doc.sculptor.beginStroke(mesh, name, doc.activeLayer)) {
        error = name + ": the active layer's weight is 0";
        return 0;
    }
    doc.sculptor.moveVertices(mesh, doc.bvh, verts, d);
    UndoRecord r = doc.sculptor.endStroke();
    if (!r.empty()) doc.undo.push(std::move(r));
    return verts.size();
}

void registerDeformCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    struct Spec {
        const char* name;
        DeformOp op;
        const char* what;
    };
    static const Spec kSpecs[] = {
        {"deform_polish", DeformOp::Polish,
         "Polish the active Part: flatten fine noise and small bumps while keeping flat areas and sharp edges."},
        {"deform_relax", DeformOp::Relax,
         "Relax the active Part: even out the spacing of its vertices along the surface without changing its shape."},
        {"deform_smooth", DeformOp::Smooth,
         "Smooth the whole active Part (Laplacian; keep_volume=true for Taubin smoothing, which doesn't shrink it)."},
        {"deform_balloon", DeformOp::Balloon,
         "Balloon (ZBrush users: Inflate Balloon): fill the active Part out toward a rounder volume; a negative "
         "amount lets the air out."},
    };
    for (const Spec& sp : kSpecs) {
        std::vector<ParamSpec> params = {
            {"amount", T::Number,
             sp.op == DeformOp::Balloon ? "How much, -100 to 100 (default 50)." : "How much, 0 to 100 (default 50).",
             false},
            {"masked", T::Bool, "Masked parts stay (default true; false ignores the mask).", false}};
        if (sp.op == DeformOp::Smooth) params.push_back({"keep_volume", T::Bool, "Taubin smoothing (default false).", false});
        const DeformOp op = sp.op;
        reg.add({sp.name,
                 std::string(sp.what) + " Mask-aware (partly masked vertices move partly), hidden faces and open "
                                        "borders stay; one undo step.",
                 params, [op](Document& doc, const Args& a) -> CommandResult {
                     DeformOptions o;
                     const double lo = op == DeformOp::Balloon ? -100.0 : 0.0;
                     o.amount = float(std::clamp(argNumber(a, "amount", 50.0), lo, 100.0));
                     o.useMask = argBool(a, "masked", true);
                     o.keepVolume = argBool(a, "keep_volume", false);
                     if (o.amount == 0.0f) return {true, std::string(deformName(op)) + ": amount 0, nothing to do"};
                     std::string err;
                     const size_t moved = deformActivePart(doc, op, o, err);
                     if (moved == 0) return {false, err};
                     return {true, std::string(deformName(op)) + " " + std::to_string(int(std::lround(o.amount))) + ": moved " +
                                       std::to_string(moved) + " vertices"};
                 }});
    }
}

} // namespace sf
