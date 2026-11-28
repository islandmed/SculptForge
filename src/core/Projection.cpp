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

namespace {
// The nearest point q of `o` counts when the way to it is within acos(minCos) of the normal's line (no normal: any).
bool nearestIsAlongNormal(const glm::vec3& o, const glm::vec3& q, const glm::vec3& n, float minCos) {
    if (minCos <= -1.0f || glm::dot(n, n) < 0.5f) return true;
    const glm::vec3 d = q - o;
    const float len = glm::length(d);
    return len <= 1e-12f || std::abs(glm::dot(d, n)) >= minCos * len;
}
} // namespace

bool computeProjection(const Mesh& target, const std::vector<const Mesh*>& sources, const ProjectOptions& opt,
                       std::vector<glm::vec3>& positions, std::vector<glm::vec3>* colors, ProjectStats& stats,
                       std::string& error, const ProjectProgress& progress) {
    const auto start = std::chrono::steady_clock::now();
    stats = {};
    const size_t nv = target.vertexCount();
    if (nv == 0) {
        error = "Project Detail: the active Part is empty";
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
        error = "Project Detail needs another visible Part to project from";
        return false;
    }
    TriangleBVH bvh;
    bvh.build(std::move(P), std::move(T));
    if (progress && !progress(0.3f)) {
        error = "Project Detail cancelled";
        return false;
    }

    const float reach = std::max(opt.distance, 1e-6f) * glm::length(target.bounds().extent());
    const bool doColors = colors && opt.vertexpaint && anyColor;
    positions = target.positions;
    if (doColors) *colors = target.hasColors() ? target.colors : std::vector<glm::vec3>(nv, glm::vec3(1.0f));
    std::vector<float> moved(nv, 0.0f); // weight each vertex was projected with (0 = untouched)
    std::vector<float> follow(nv, 0.0f); // weight of a vertex whose nearest point lay off to the side (it follows its ring)
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
            // Only surface that faces the way the vertex does (rays go on past the far wall of a crevice).
            FacingFilter ff;
            if (hasN) ff = {glm::normalize(n), opt.facing};
            const FacingFilter* facing = hasN && opt.facing > -1.0f ? &ff : nullptr;
            const bool hitOut = hasN && bvh.raycast(o, n, reach, out, facing);
            const bool hitIn = hasN && bvh.raycast(o, -n, reach, in, facing);
            bool found = true;
            glm::vec3 to;
            if (hitOut && hitIn && opt.mean) {
                to = 0.5f * (out.position + in.position);
                hit = out.distance <= in.distance ? out : in;
            } else if (hitOut || hitIn) {
                hit = hitOut && (!hitIn || out.distance <= in.distance) ? out : in;
                to = hit.position;
            } else if (bvh.closestPoint(o, reach, hit, facing)) { // no surface straight along the normal: the nearest point
                if (nearestIsAlongNormal(o, hit.position, hasN ? glm::normalize(n) : glm::vec3(0.0f), opt.fallbackCos)) {
                    to = hit.position;
                } else { // ... which is off to the side: it follows its neighbours below
                    follow[v] = w;
                    found = false;
                }
            } else {
                found = false;
            }
            if (!found) {
                if (follow[v] <= 0.0f) ++m; // (a vertex that follows its ring is counted below)
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
        error = "Project Detail cancelled";
        return false;
    }
    // A vertex whose nearest source point lay off to the side takes the median offset (and mean colour) of its projected
    // neighbours; a few rounds let it reach across a patch of such vertices. (In a concave pocket many vertices share one
    // nearest point: taking it would pinch them together; leaving them behind would leave steps.)
    if (opt.geometry || doColors) {
        std::vector<uint32_t> todo;
        for (size_t v = 0; v < nv; ++v)
            if (follow[v] > 0.0f) todo.push_back(uint32_t(v));
        std::vector<std::pair<uint32_t, glm::vec3>> got;
        std::vector<float> c[3];
        for (int round = 0; round < 4 && !todo.empty(); ++round) {
            got.clear();
            std::vector<uint32_t> rest;
            std::vector<glm::vec3> gotColor;
            for (uint32_t v : todo) {
                for (auto& x : c) x.clear();
                glm::vec3 col(0.0f);
                for (uint32_t u : target.neighbors(v)) {
                    if (moved[u] <= 0.0f) continue;
                    const glm::vec3 d = positions[u] - target.positions[u];
                    for (int k = 0; k < 3; ++k) c[k].push_back(d[k]);
                    if (doColors) col += (*colors)[u];
                }
                if (c[0].empty()) {
                    rest.push_back(v);
                    continue;
                }
                glm::vec3 med;
                for (int k = 0; k < 3; ++k) {
                    std::nth_element(c[k].begin(), c[k].begin() + ptrdiff_t(c[k].size() / 2), c[k].end());
                    med[k] = c[k][c[k].size() / 2];
                }
                got.emplace_back(v, med);
                gotColor.push_back(col / float(c[0].size()));
            }
            if (got.empty()) break;
            for (size_t i = 0; i < got.size(); ++i) {
                const uint32_t v = got[i].first;
                if (opt.geometry) positions[v] = target.positions[v] + got[i].second * follow[v];
                if (doColors) (*colors)[v] = glm::mix((*colors)[v], gotColor[i], follow[v]);
                moved[v] = follow[v];
            }
            projected += got.size();
            todo.swap(rest);
        }
        missed += todo.size(); // (nothing projected around them)
    }
    // Outlier guard: a vertex whose offset differs from its 1-ring's median offset by more than `outlier` edge lengths
    // reached across to another plate or fin instead of its own surface. It takes the ring's median offset (a few
    // rounds, so a small cluster that jumped together is pulled back too).
    if (opt.outlier > 0.0f && (opt.geometry || doColors)) {
        std::vector<glm::vec3> off(nv, glm::vec3(0.0f)), next;
        std::vector<uint8_t> bad(nv, 0);
        for (size_t v = 0; v < nv; ++v)
            if (moved[v] > 0.0f) off[v] = positions[v] - target.positions[v];
        std::atomic<size_t> nbad{0};
        for (int round = 0; round < 3; ++round) {
            next = off;
            std::atomic<size_t> changed{0};
            parallelFor(nv, [&](size_t b, size_t e) {
                std::vector<float> c[3];
                size_t count = 0;
                for (size_t v = b; v < e; ++v) {
                    if (moved[v] <= 0.0f) continue;
                    const auto ring = target.neighbors(uint32_t(v));
                    float edge = 0.0f;
                    for (auto& x : c) x.clear();
                    for (uint32_t u : ring) {
                        edge += glm::length(target.positions[u] - target.positions[v]);
                        if (moved[u] <= 0.0f) continue;
                        for (int k = 0; k < 3; ++k) c[k].push_back(off[u][k]);
                    }
                    if (c[0].size() < 3) continue;
                    edge /= float(ring.size());
                    glm::vec3 med;
                    for (int k = 0; k < 3; ++k) {
                        std::nth_element(c[k].begin(), c[k].begin() + ptrdiff_t(c[k].size() / 2), c[k].end());
                        med[k] = c[k][c[k].size() / 2];
                    }
                    if (glm::length(off[v] - med) > opt.outlier * edge) {
                        next[v] = med;
                        if (!bad[v]) bad[v] = 1, ++count; // (each vertex counted once; flags are per-vertex, no race)
                        ++changed;
                    }
                }
                nbad += count;
            });
            off.swap(next);
            if (changed == 0) break;
        }
        if (nbad > 0) {
            std::vector<glm::vec3> col;
            if (doColors) col = *colors;
            parallelFor(nv, [&](size_t b, size_t e) {
                for (size_t v = b; v < e; ++v) {
                    if (!bad[v]) continue;
                    if (opt.geometry) positions[v] = target.positions[v] + off[v];
                    if (!doColors) continue;
                    glm::vec3 sum(0.0f);
                    int n = 0;
                    for (uint32_t u : target.neighbors(uint32_t(v)))
                        if (moved[u] > 0.0f && !bad[u]) sum += col[u], ++n;
                    if (n > 0) (*colors)[v] = sum / float(n);
                }
            });
        }
        stats.outliers = nbad;
    }
    // Blur: smooth the offsets of the projected vertices.
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
    for (int i = 0; i < doc.partCount(); ++i) {
        if (i == doc.activePart) continue;
        if (source >= 0 ? i != source : !doc.parts[size_t(i)].visible) continue;
        if (doc.partMesh(i).triangleCount() > 0) sources.push_back(&doc.partMesh(i));
    }
    if (sources.empty()) {
        message = source >= 0 ? "Project Detail: that Part can't be projected from"
                              : "Project Detail needs another visible Part to project from (the source)";
        return false;
    }
    if (!opt.geometry && !opt.vertexpaint) {
        message = "Project Detail: nothing to project (turn on Geometry or Vertex Paint)";
        return false;
    }
    Mesh& m = doc.mesh;
    const int layer = m.layers.empty() ? -1 : doc.activeLayer;
    const float weight = m.layerTargetWeight(layer);
    if (opt.geometry && !m.layers.empty() && std::abs(weight) < 1e-4f) {
        message = "Project Detail: the active sculpt layer's weight is 0";
        return false;
    }
    if (opt.vertexpaint && !m.paintLayers.empty() && !paintLayerEditable(m, m.activePaintLayer)) {
        message = "Project Detail: the active paint layer is locked or hidden";
        return false;
    }
    std::vector<glm::vec3> positions, colors;
    ProjectStats st;
    if (!computeProjection(m, sources, opt, positions, opt.vertexpaint ? &colors : nullptr, st, message)) return false;

    // One undo step: the shape (active sculpt layer) and the colours (active paint layer), or a snapshot when
    // the Part had no vertex paint yet, so undo takes the attribute away again.
    UndoRecord r;
    if (st.colors && !m.hasColors()) {
        r.mesh = std::make_unique<Mesh>(m);
    } else if (st.colors) {
        r = UndoRecord::captureColors(m, "");
    }
    r.label = "Project Detail";
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
    std::snprintf(buf, sizeof buf, "Project Detail: %zu vertices projected%s from %zu Part%s in %.2f s%s", st.projected,
                  st.colors ? " (shape and vertex paint)" : "", sources.size(), sources.size() == 1 ? "" : "s", st.seconds,
                  st.missed ? (", " + std::to_string(st.missed) + " found nothing within Dist").c_str() : "");
    message = buf;
    if (statsOut) *statsOut = st;
    return true;
}

} // namespace sf
