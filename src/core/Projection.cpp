// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Projection.h"

#include "core/PaintLayer.h"
#include "core/Parallel.h"
#include "core/SurfaceQuery.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace sf {

bool computeProjection(const Mesh& target, const std::vector<const Mesh*>& sources, const ProjectOptions& opt,
                       std::vector<glm::vec3>& positions, std::vector<glm::vec3>* colors, ProjectStats& stats,
                       std::string& error, const ProjectProgress& progress) {
    const auto start = std::chrono::steady_clock::now();
    stats = {};
    const size_t nv = target.vertexCount();
    if (nv == 0) {
        error = "Project All: the active Part is empty";
        return false;
    }
    // One triangle soup of every visible source triangle, with its colours.
    std::vector<glm::vec3> P, C;
    std::vector<glm::uvec3> T;
    bool anyColor = false;
    for (const Mesh* s : sources) anyColor |= s->hasColors();
    for (const Mesh* s : sources) {
        const uint32_t first = uint32_t(P.size());
        P.insert(P.end(), s->positions.begin(), s->positions.end());
        if (anyColor) {
            if (s->hasColors()) C.insert(C.end(), s->colors.begin(), s->colors.end());
            else C.insert(C.end(), s->vertexCount(), glm::vec3(1.0f));
        }
        for (uint32_t t = 0; t < s->triangleCount(); ++t)
            if (!s->triangleHidden(t)) T.push_back(s->triangles[t] + glm::uvec3(first));
    }
    if (T.empty()) {
        error = "Project All needs another visible Part to project from";
        return false;
    }
    TriangleBVH bvh;
    bvh.build(std::move(P), std::move(T));
    if (progress && !progress(0.3f)) {
        error = "Project All cancelled";
        return false;
    }

    const float reach = std::max(opt.distance, 1e-6f) * glm::length(target.bounds().extent());
    const bool doColors = colors && opt.polypaint && anyColor;
    positions = target.positions;
    if (doColors) *colors = target.hasColors() ? target.colors : std::vector<glm::vec3>(nv, glm::vec3(1.0f));
    std::vector<float> moved(nv, 0.0f); // weight each vertex was projected with (0 = untouched)
    std::atomic<size_t> projected{0}, missed{0}, masked{0}, done{0};
    std::atomic<bool> cancel{false};
    const auto& tp = bvh.triangles();
    const bool hasNormals = target.normals.size() == nv;
    parallelFor(nv, [&](size_t b, size_t e) {
        size_t p = 0, m = 0, k = 0;
        for (size_t v = b; v < e; ++v) {
            if (cancel.load(std::memory_order_relaxed)) break;
            const float w = 1.0f - target.maskAt(uint32_t(v));
            if (w <= 0.0f || (target.hasHidden() && target.vertexHidden(uint32_t(v)))) {
                ++k;
                continue;
            }
            const glm::vec3 o = target.positions[v];
            const glm::vec3 n = hasNormals ? target.normals[v] : glm::vec3(0.0f);
            SurfaceHit out, in, hit;
            const bool hasN = glm::dot(n, n) > 0.25f;
            const bool hitOut = hasN && bvh.raycast(o, n, reach, out);
            const bool hitIn = hasN && bvh.raycast(o, -n, reach, in);
            bool found = true;
            glm::vec3 to;
            if (hitOut && hitIn && opt.mean) {
                to = 0.5f * (out.position + in.position);
                hit = out.distance <= in.distance ? out : in;
            } else if (hitOut || hitIn) {
                hit = hitOut && (!hitIn || out.distance <= in.distance) ? out : in;
                to = hit.position;
            } else if (bvh.closestPoint(o, reach, hit)) { // no surface straight along the normal: nearest point
                to = hit.position;
            } else {
                found = false;
            }
            if (!found) {
                ++m;
                continue;
            }
            ++p;
            moved[v] = w;
            if (opt.geometry) positions[v] = glm::mix(o, to, w);
            if (doColors) {
                const glm::uvec3 t = tp[hit.prim];
                const glm::vec3 c = hit.bary.x * C[t.x] + hit.bary.y * C[t.y] + hit.bary.z * C[t.z];
                (*colors)[v] = glm::mix((*colors)[v], c, w);
            }
        }
        projected += p, missed += m, masked += k;
        const size_t d = done += e - b;
        if (progress && !progress(0.3f + 0.6f * float(d) / float(nv))) cancel = true;
    });
    if (cancel) {
        error = "Project All cancelled";
        return false;
    }
    // PA Blur: smooth the offsets of the projected vertices.
    if (opt.geometry && opt.blur > 0) {
        std::vector<glm::vec3> d(nv), next(nv);
        for (size_t v = 0; v < nv; ++v) d[v] = positions[v] - target.positions[v];
        for (int it = 0; it < opt.blur; ++it) {
            parallelFor(nv, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    const auto ring = target.neighbors(uint32_t(v));
                    if (moved[v] <= 0.0f || ring.empty()) {
                        next[v] = d[v];
                        continue;
                    }
                    glm::vec3 s(0.0f);
                    for (uint32_t u : ring) s += d[u];
                    next[v] = glm::mix(d[v], s / float(ring.size()), 0.5f * moved[v]);
                }
            });
            d.swap(next);
        }
        for (size_t v = 0; v < nv; ++v)
            if (moved[v] > 0.0f) positions[v] = target.positions[v] + d[v];
    }
    stats.projected = projected;
    stats.missed = missed;
    stats.masked = masked;
    stats.colors = doColors;
    stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return true;
}

bool projectAll(Document& doc, const ProjectOptions& opt, std::string& message, int source, ProjectStats* statsOut) {
    std::vector<const Mesh*> sources;
    for (int i = 0; i < doc.subtoolCount(); ++i) {
        if (i == doc.activeSubtool) continue;
        if (source >= 0 ? i != source : !doc.subtools[size_t(i)].visible) continue;
        if (doc.subtoolMesh(i).triangleCount() > 0) sources.push_back(&doc.subtoolMesh(i));
    }
    if (sources.empty()) {
        message = source >= 0 ? "Project All: that Part can't be projected from"
                              : "Project All needs another visible Part to project from (the source)";
        return false;
    }
    if (!opt.geometry && !opt.polypaint) {
        message = "Project All: nothing to project (turn on Geometry or Vertex Paint)";
        return false;
    }
    Mesh& m = doc.mesh;
    const int layer = m.layers.empty() ? -1 : doc.activeLayer;
    const float weight = m.layerTargetWeight(layer);
    if (opt.geometry && !m.layers.empty() && std::abs(weight) < 1e-4f) {
        message = "Project All: the active sculpt layer's weight is 0";
        return false;
    }
    if (opt.polypaint && !m.paintLayers.empty() && !paintLayerEditable(m, m.activePaintLayer)) {
        message = "Project All: the active paint layer is locked or hidden";
        return false;
    }
    std::vector<glm::vec3> positions, colors;
    ProjectStats st;
    if (!computeProjection(m, sources, opt, positions, opt.polypaint ? &colors : nullptr, st, message)) return false;

    // One undo step: the shape (active sculpt layer) and the colours (active paint layer), or a snapshot when
    // the SubTool had no polypaint yet, so undo takes the attribute away again.
    UndoRecord r;
    if (st.colors && !m.hasColors()) {
        r.mesh = std::make_unique<Mesh>(m);
    } else if (st.colors) {
        r = UndoRecord::captureColors(m, "");
    }
    r.label = "Project All";
    const size_t nv = m.vertexCount();
    if (opt.geometry) {
        std::vector<glm::vec3>& tgt = m.layerTarget(layer);
        std::vector<uint32_t> changed;
        for (uint32_t v = 0; v < nv; ++v)
            if (positions[v] != m.positions[v]) changed.push_back(v);
        if (!r.mesh) {
            r.layer = layer;
            for (uint32_t v : changed) r.positions.add(v, tgt[v]);
        }
        for (uint32_t v : changed) tgt[v] += (positions[v] - m.positions[v]) / weight;
        if (!m.layers.empty()) m.evaluate(changed);
        m.recomputeAllNormals();
    }
    if (st.colors) {
        m.ensureColors();
        for (uint32_t v = 0; v < nv; ++v)
            if (colors[v] != m.colors[v]) paintVertex(m, v, colors[v], 1.0f);
    }
    m.markAllDirty();
    doc.bvh.build(m);
    if (!r.empty()) doc.undo.push(std::move(r));
    doc.modified = true;

    char buf[256];
    std::snprintf(buf, sizeof buf, "Project All: %zu vertices projected%s from %zu Part%s in %.2f s%s", st.projected,
                  st.colors ? " (shape and vertex paint)" : "", sources.size(), sources.size() == 1 ? "" : "s", st.seconds,
                  st.missed ? (", " + std::to_string(st.missed) + " found nothing within Dist").c_str() : "");
    message = buf;
    if (statsOut) *statsOut = st;
    return true;
}

} // namespace sf
