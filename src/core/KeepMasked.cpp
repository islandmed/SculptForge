// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/KeepMasked.h"

#include "core/Polygroups.h"
#include "core/QRemesh.h"
#include "core/SubTools.h"

#include <algorithm>
#include <string>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace sf {

std::vector<uint8_t> keepMaskedTriangles(const Mesh& mesh) {
    if (!mesh.hasMask() || mesh.mask.size() != mesh.vertexCount()) return {};
    std::vector<uint8_t> keep(mesh.triangleCount(), 0);
    for (size_t t = 0; t < keep.size(); ++t) {
        const glm::uvec3& tri = mesh.triangles[t];
        keep[t] = mesh.mask[tri.x] >= kKeepMaskedThreshold && mesh.mask[tri.y] >= kKeepMaskedThreshold &&
                  mesh.mask[tri.z] >= kKeepMaskedThreshold;
    }
    return keep;
}

Mesh unmaskedPart(const Mesh& source, const std::vector<uint8_t>& keep) {
    std::vector<uint8_t> rest(source.triangleCount(), 1);
    for (size_t t = 0; t < rest.size() && t < keep.size(); ++t) rest[t] = !keep[t];
    return extractTriangles(source, rest);
}

namespace {

uint64_t directedKey(uint32_t a, uint32_t b) { return (uint64_t(a) << 32) | b; }

// The closed loops of open edges of `tris`, each in the direction its triangle runs along it (the triangle on
// its left). An edge is open where more triangles run along it one way than the other, so an edge where the
// surface meets itself (four triangles, as voxel remeshing leaves where two parts of a model touch) is not
// open (#67: the stitch used to refuse every such surface). False when the open edges aren't a set of simple
// loops: an edge open twice over, or a vertex where two loops meet (each such vertex is added to `pinches`).
bool boundaryLoops(const std::vector<glm::uvec3>& tris, std::vector<std::vector<uint32_t>>& loops,
                   std::vector<uint32_t>* pinches = nullptr) {
    loops.clear();
    std::unordered_map<uint64_t, int> used;
    used.reserve(tris.size() * 3);
    for (const glm::uvec3& t : tris)
        for (int e = 0; e < 3; ++e)
            if (t[e] != t[(e + 1) % 3]) ++used[directedKey(t[e], t[(e + 1) % 3])];
    std::unordered_map<uint32_t, uint32_t> next;
    std::unordered_map<uint32_t, int> outs, ins;
    bool ok = true;
    for (const auto& [key, n] : used) {
        const uint32_t a = uint32_t(key >> 32), b = uint32_t(key & 0xffffffffu);
        const auto back = used.find(directedKey(b, a));
        const int open = n - (back == used.end() ? 0 : back->second);
        if (open <= 0) continue;
        if (open > 1) {
            ok = false;
            if (pinches) pinches->push_back(a);
        }
        next.emplace(a, b);
        ++outs[a];
        ++ins[b];
    }
    for (const auto& [v, n] : outs)
        if (n > 1 || ins[v] > 1) {
            ok = false;
            if (pinches) pinches->push_back(v);
        }
    if (!ok) return false;
    std::unordered_set<uint32_t> seen;
    std::vector<uint32_t> starts;
    for (const auto& [a, b] : next) starts.push_back(a);
    std::sort(starts.begin(), starts.end()); // the same loops in the same order every run
    for (uint32_t start : starts) {
        if (seen.count(start)) continue;
        std::vector<uint32_t> loop;
        uint32_t v = start;
        do {
            if (!seen.insert(v).second) return false;
            loop.push_back(v);
            const auto it = next.find(v);
            if (it == next.end()) return false;
            v = it->second;
        } while (v != start);
        if (loop.size() < 3) return false;
        loops.push_back(std::move(loop));
    }
    return true;
}

// For each point in `query`, the value of the nearest point in `points` (transferVertexAttributes' search,
// with the value carried in the colour's red channel: whole numbers come through exactly).
std::vector<float> nearestValues(std::vector<glm::vec3> points, const std::vector<float>& values, std::vector<glm::vec3> query) {
    Mesh from, to;
    from.positions = std::move(points);
    from.colors.reserve(values.size());
    for (float v : values) from.colors.emplace_back(v, 0.0f, 0.0f);
    to.positions = std::move(query);
    transferVertexAttributes(from, to);
    std::vector<float> out(to.positions.size(), -1.0f);
    for (size_t i = 0; i < out.size() && i < to.colors.size(); ++i) out[i] = to.colors[i].x;
    return out;
}

// The kept triangles as their own plain mesh (no layers, levels, paint layers or texture), and which source
// vertex each of its vertices is.
Mesh keptPiece(const Mesh& source, const std::vector<uint8_t>& keep, std::vector<uint32_t>& sourceVertex) {
    const size_t nt = source.triangleCount();
    std::vector<uint32_t> remap(source.vertexCount(), ~0u), triRemap(nt, ~0u);
    sourceVertex.clear();
    Mesh k;
    for (uint32_t t = 0; t < nt; ++t) {
        if (!keep[t]) continue;
        glm::uvec3 tri = source.triangles[t];
        for (int c = 0; c < 3; ++c) {
            uint32_t& r = remap[tri[c]];
            if (r == ~0u) {
                r = uint32_t(sourceVertex.size());
                sourceVertex.push_back(tri[c]);
            }
            tri[c] = r;
        }
        triRemap[t] = uint32_t(k.triangles.size());
        k.triangles.push_back(tri);
    }
    for (uint32_t v : sourceVertex) k.positions.push_back(source.positions[v]);
    if (source.mask.size() == source.vertexCount())
        for (uint32_t v : sourceVertex) k.mask.push_back(source.mask[v]);
    if (source.colors.size() == source.vertexCount())
        for (uint32_t v : sourceVertex) k.colors.push_back(source.colors[v]);
    for (uint32_t t = 0; t < nt; ++t) {
        if (!keep[t]) continue;
        if (source.polygroups.size() == nt) k.polygroups.push_back(source.polygroups[t]);
        if (source.hidden.size() == nt) k.hidden.push_back(source.hidden[t]);
        if (source.uvs.size() == nt * 3)
            for (int c = 0; c < 3; ++c) k.uvs.push_back(source.uvs[size_t(t) * 3 + size_t(c)]);
        if (source.quadPartners.size() == nt) {
            const uint32_t u = source.quadPartners[t];
            k.quadPartners.push_back(u < nt && triRemap[u] != ~0u ? triRemap[u] : Mesh::kNoQuad);
        }
    }
    k.sanitizeAttributes();
    return k;
}

// Why one stitch failed, for stitchKeptTriangles to adjust the kept area and try again.
struct StitchFailure {
    enum Kind { None, KeptPinch, OpenEdge, Cut, Unmatched, Join } kind = None;
    std::vector<uint32_t> pinches;  // KeptPinch: source vertices where the masked area's edge touches itself
    std::vector<uint32_t> tooSmall; // Unmatched: a source vertex of each kept piece no cut was made around
};

// One stitch. `cutRings`: cut that many more rings of the new surface away round the masked area (a wider strip
// for the bridge, past spots where the new surface folds or meets itself next to the mask's edge).
bool stitchOnce(const Mesh& source, const std::vector<uint8_t>& keep, const Mesh& remeshed, Mesh& out,
                std::string& error, StitchFailure& why, int cutRings) {
    std::vector<uint32_t> keptSource;
    const Mesh kept = keptPiece(source, keep, keptSource);

    // The masked area's edge loops (in the kept piece's numbering). A loop made only of the source's own open
    // edges is a hole the source already had: it stays open. One partly on them can't be joined up.
    std::vector<std::vector<uint32_t>> keptLoops;
    {
        std::vector<uint32_t> pinches;
        if (!boundaryLoops(kept.triangles, keptLoops, &pinches)) {
            error = "The masked area's edge touches itself; smooth or grow the mask a little";
            why.kind = StitchFailure::KeptPinch;
            for (uint32_t v : pinches) why.pinches.push_back(keptSource[v]);
            return false;
        }
    }
    {
        std::unordered_set<uint64_t> sourceEdges;
        for (const glm::uvec3& t : source.triangles)
            for (int e = 0; e < 3; ++e) sourceEdges.insert(directedKey(t[e], t[(e + 1) % 3]));
        std::vector<std::vector<uint32_t>> inner;
        for (auto& loop : keptLoops) {
            size_t open = 0;
            for (size_t i = 0; i < loop.size(); ++i) {
                const uint32_t a = keptSource[loop[i]], b = keptSource[loop[(i + 1) % loop.size()]];
                open += !sourceEdges.count(directedKey(b, a));
            }
            if (open == loop.size()) continue;
            if (open > 0) {
                error = "The masked area reaches an open edge of the mesh; unmask the strip along the open edge";
                why.kind = StitchFailure::OpenEdge;
                return false;
            }
            inner.push_back(std::move(loop));
        }
        keptLoops = std::move(inner);
    }

    // Cut the new surface away over the masked area and next to it: every new triangle with a corner whose
    // nearest source vertex belongs to a kept triangle (so the cut lies just outside the kept piece), and the
    // other half of any quad cut in two.
    std::vector<float> inKept(source.vertexCount(), 0.0f);
    for (uint32_t v : keptSource) inKept[v] = 1.0f;
    const std::vector<float> nearKept = nearestValues(source.positions, inKept, remeshed.positions);
    const size_t rt = remeshed.triangleCount();
    std::vector<uint8_t> stay(rt, 1);
    for (size_t t = 0; t < rt; ++t) {
        const glm::uvec3& tri = remeshed.triangles[t];
        if (nearKept[tri.x] > 0.5f || nearKept[tri.y] > 0.5f || nearKept[tri.z] > 0.5f) stay[t] = 0;
    }
    auto dropHalfQuads = [&] {
        if (remeshed.quadPartners.size() != rt) return;
        for (size_t t = 0; t < rt; ++t) {
            const uint32_t u = remeshed.quadPartners[t];
            if (u < rt && !stay[u]) stay[t] = 0;
        }
    };
    dropHalfQuads();
    for (int ring = 0; ring < cutRings; ++ring) {
        std::vector<uint8_t> cutCorner(remeshed.vertexCount(), 0);
        for (size_t t = 0; t < rt; ++t)
            if (!stay[t])
                for (int c = 0; c < 3; ++c) cutCorner[remeshed.triangles[t][c]] = 1;
        for (size_t t = 0; t < rt; ++t) {
            const glm::uvec3& tri = remeshed.triangles[t];
            if (cutCorner[tri.x] || cutCorner[tri.y] || cutCorner[tri.z]) stay[t] = 0;
        }
        dropHalfQuads();
    }
    // Where the cut leaves two open loops meeting at a vertex (or runs along a spot where the new surface meets
    // itself), cut the triangles around it too.
    for (int pass = 0; pass < 32; ++pass) {
        std::vector<glm::uvec3> stayed;
        for (size_t t = 0; t < rt; ++t)
            if (stay[t]) stayed.push_back(remeshed.triangles[t]);
        std::vector<std::vector<uint32_t>> loops;
        std::vector<uint32_t> pinches;
        if (boundaryLoops(stayed, loops, &pinches) || pinches.empty()) break;
        const std::unordered_set<uint32_t> pinch(pinches.begin(), pinches.end());
        for (size_t t = 0; t < rt; ++t) {
            const glm::uvec3& tri = remeshed.triangles[t];
            if (pinch.count(tri.x) || pinch.count(tri.y) || pinch.count(tri.z)) stay[t] = 0;
        }
        dropHalfQuads();
    }

    // Match each of the kept piece's loops with the cut's loop around it (the cut's loop whose points are
    // mostly nearest to it). A small leftover piece of the new surface with a loop nothing matches (a sliver
    // inside the masked area) is dropped; anything else that doesn't match up is reported.
    Mesh rest;
    std::vector<std::vector<uint32_t>> restLoops;
    std::vector<int> match(keptLoops.size(), -1);
    for (int attempt = 0; attempt < 2; ++attempt) {
        rest = extractTriangles(remeshed, stay);
        rest.layers.clear();
        rest.base.clear();
        if (!boundaryLoops(rest.triangles, restLoops)) {
            error = "Couldn't cut the remeshed surface cleanly around the mask";
            why.kind = StitchFailure::Cut;
            return false;
        }
        std::vector<glm::vec3> keptPoints, restPoints;
        std::vector<float> keptLoopId;
        for (size_t l = 0; l < keptLoops.size(); ++l)
            for (uint32_t v : keptLoops[l]) {
                keptPoints.push_back(kept.positions[v]);
                keptLoopId.push_back(float(l));
            }
        for (const auto& loop : restLoops)
            for (uint32_t v : loop) restPoints.push_back(rest.positions[v]);
        const std::vector<float> nearestLoop =
            keptPoints.empty() ? std::vector<float>(restPoints.size(), -1.0f) : nearestValues(keptPoints, keptLoopId, restPoints);
        std::vector<int> votedFor(restLoops.size(), -1);
        std::vector<uint8_t> voted(keptLoops.size(), 0); // a cut's loop runs near it at all
        size_t at = 0;
        for (size_t r = 0; r < restLoops.size(); ++r) {
            std::map<int, size_t> votes;
            for (size_t i = 0; i < restLoops[r].size(); ++i) {
                const int id = int(nearestLoop[at++]);
                ++votes[id];
                if (id >= 0 && size_t(id) < voted.size()) voted[size_t(id)] = 1;
            }
            size_t best = 0;
            for (const auto& [id, n] : votes)
                if (id >= 0 && n > best) {
                    best = n;
                    votedFor[r] = id;
                }
        }
        std::fill(match.begin(), match.end(), -1);
        for (size_t r = 0; r < restLoops.size(); ++r) {
            const int k = votedFor[r];
            if (k >= 0 && (match[size_t(k)] < 0 || restLoops[r].size() > restLoops[size_t(match[size_t(k)])].size()))
                match[size_t(k)] = int(r);
        }
        std::vector<uint8_t> matched(restLoops.size(), 0);
        for (int r : match)
            if (r >= 0) matched[size_t(r)] = 1;
        if (std::find(match.begin(), match.end(), -1) != match.end()) {
            // No cut's loop of its own: a masked spot smaller than the new surface's polygons (no new vertex lies
            // nearest to it, so nothing was cut around it), or two masked areas so close that one cut opened
            // round both.
            error = "Part of the mask is too thin for this resolution";
            why.kind = StitchFailure::Unmatched;
            for (size_t l = 0; l < keptLoops.size(); ++l)
                if (match[l] < 0 && !voted[l]) why.tooSmall.push_back(keptSource[keptLoops[l][0]]);
            return false;
        }
        if (std::find(matched.begin(), matched.end(), 0) == matched.end()) break;
        if (attempt == 1) {
            error = "Couldn't match the remeshed surface to the mask's edge";
            why.kind = StitchFailure::Unmatched;
            return false;
        }
        // Leftover loops: drop the small pieces of the new surface they belong to, then look again.
        std::vector<uint32_t> parent(rest.vertexCount());
        for (uint32_t v = 0; v < parent.size(); ++v) parent[v] = v;
        auto find = [&](uint32_t v) {
            while (parent[v] != v) v = parent[v] = parent[parent[v]];
            return v;
        };
        for (const glm::uvec3& t : rest.triangles) {
            parent[find(t.y)] = find(t.x);
            parent[find(t.z)] = find(t.x);
        }
        std::unordered_map<uint32_t, size_t> pieceTris;
        for (const glm::uvec3& t : rest.triangles) ++pieceTris[find(t.x)];
        std::unordered_set<uint32_t> neededPieces, strayPieces;
        for (size_t r = 0; r < restLoops.size(); ++r) (matched[r] ? neededPieces : strayPieces).insert(find(restLoops[r][0]));
        for (uint32_t p : strayPieces)
            if (neededPieces.count(p) || pieceTris[p] * 10 > rest.triangleCount()) {
                error = "Couldn't match the remeshed surface to the mask's edge";
                why.kind = StitchFailure::Unmatched;
                return false;
            }
        // Map the stray pieces back to the remeshed triangles (extractTriangles keeps the order).
        size_t r = 0;
        for (size_t t = 0; t < rt; ++t)
            if (stay[t] && strayPieces.count(find(rest.triangles[r++].x))) stay[t] = 0;
    }

    Mesh merged = mergeMeshes({&kept, &rest});
    const uint32_t offset = uint32_t(kept.vertexCount());
    const size_t keptTris = kept.triangleCount();
    const uint32_t bridgeGroup = merged.polygroups.size() > keptTris ? merged.polygroups[keptTris] : 0u;
    std::vector<glm::uvec3> bridge;
    for (size_t l = 0; l < keptLoops.size(); ++l) {
        const auto& P = keptLoops[l];
        const auto& Q = restLoops[size_t(match[l])];
        // The cut's loop runs the other way round the gap: walk it backwards from its point nearest P's start.
        size_t j0 = 0;
        float bestD = std::numeric_limits<float>::max();
        for (size_t j = 0; j < Q.size(); ++j) {
            const float d = glm::distance(kept.positions[P[0]], rest.positions[Q[j]]);
            if (d < bestD) {
                bestD = d;
                j0 = j;
            }
        }
        const size_t n = P.size(), m = Q.size();
        auto p = [&](size_t i) { return P[i % n]; };
        auto s = [&](size_t k) { return Q[(j0 + m - (k % m)) % m] + offset; };
        auto pos = [&](uint32_t v) { return merged.positions[v]; };
        // Zip the two loops together, each step taking the shorter new diagonal. Every triangle runs along both
        // loops the opposite way to the surfaces they edge, so the result stays consistently wound.
        size_t i = 0, k = 0;
        while (i < n || k < m) {
            const bool advanceP =
                k >= m || (i < n && glm::distance(pos(p(i + 1)), pos(s(k))) <= glm::distance(pos(p(i)), pos(s(k + 1))));
            if (advanceP) {
                bridge.emplace_back(p(i + 1), p(i), s(k));
                ++i;
            } else {
                bridge.emplace_back(s(k), s(k + 1), p(i));
                ++k;
            }
        }
    }
    for (const glm::uvec3& t : bridge) {
        merged.triangles.push_back(t);
        if (!merged.polygroups.empty()) merged.polygroups.push_back(bridgeGroup);
        if (!merged.hidden.empty()) merged.hidden.push_back(0);
        if (!merged.uvs.empty()) merged.uvs.insert(merged.uvs.end(), 3, glm::vec2(0.0f));
        if (!merged.quadPartners.empty()) merged.quadPartners.push_back(Mesh::kNoQuad);
    }
    merged.sanitizeAttributes();
    merged.rebuildTopology();
    merged.recomputeAllNormals();

    // The join must not add defects: the result may only meet itself where the new surface or the kept piece
    // already did (voxel remeshing leaves such spots where parts of a model touch), and stays closed when the
    // source was.
    const TopologyReport after = topologyReport(merged), restReport = topologyReport(rest), keptReport = topologyReport(kept);
    if (after.nonManifoldEdges > restReport.nonManifoldEdges + keptReport.nonManifoldEdges ||
        after.nonManifoldVertices > restReport.nonManifoldVertices + keptReport.nonManifoldVertices ||
        (topologyReport(source).boundaryEdges == 0 && after.boundaryEdges != 0)) {
        error = "Couldn't join the masked area to the remeshed surface cleanly";
        why.kind = StitchFailure::Join;
        return false;
    }
    out = std::move(merged);
    return true;
}

// Every triangle of `keep` connected (through shared corners) to one with a corner in `seeds`, unkept.
void dropPieces(const Mesh& source, std::vector<uint8_t>& keep, const std::vector<uint32_t>& seeds) {
    std::vector<uint32_t> parent(source.vertexCount());
    for (uint32_t v = 0; v < parent.size(); ++v) parent[v] = v;
    auto find = [&](uint32_t v) {
        while (parent[v] != v) v = parent[v] = parent[parent[v]];
        return v;
    };
    for (size_t t = 0; t < keep.size(); ++t)
        if (keep[t]) {
            const glm::uvec3& tri = source.triangles[t];
            parent[find(tri.y)] = find(tri.x);
            parent[find(tri.z)] = find(tri.x);
        }
    std::unordered_set<uint32_t> drop;
    for (uint32_t v : seeds) drop.insert(find(v));
    for (size_t t = 0; t < keep.size(); ++t)
        if (keep[t] && drop.count(find(source.triangles[t].x))) keep[t] = 0;
}

// Morphological closing of the kept area by `rings` rings of vertices (grown, then shrunk back): gaps and notches
// up to about 2 x rings edges across are filled with the source's own triangles, the rest of the edge stays.
void closeKept(const Mesh& source, std::vector<uint8_t>& keep, int rings) {
    const size_t nv = source.vertexCount();
    std::vector<uint8_t> in(nv, 0), next(nv, 0);
    for (size_t t = 0; t < keep.size(); ++t)
        if (keep[t])
            for (int c = 0; c < 3; ++c) in[source.triangles[t][c]] = 1;
    for (int pass = 0; pass < 2 * rings; ++pass) {
        const bool grow = pass < rings;
        for (uint32_t v = 0; v < nv; ++v) {
            uint8_t x = in[v];
            for (uint32_t n : source.neighbors(v)) x = grow ? uint8_t(x | in[n]) : uint8_t(x & in[n]);
            next[v] = x;
        }
        in.swap(next);
    }
    for (size_t t = 0; t < keep.size(); ++t) {
        const glm::uvec3& tri = source.triangles[t];
        if (in[tri.x] && in[tri.y] && in[tri.z]) keep[t] = 1;
    }
}

} // namespace

bool stitchKeptTriangles(const Mesh& source, const std::vector<uint8_t>& keepIn, const Mesh& remeshed, Mesh& out,
                         std::string& error, std::string* note) {
    const size_t nt = source.triangleCount();
    if (keepIn.size() != nt || std::find(keepIn.begin(), keepIn.end(), 1) == keepIn.end()) {
        error = "Nothing is masked";
        return false;
    }
    // A mask edge the stitch can't follow is tidied and tried again (#67): where the edge touches itself, the
    // triangles at that corner are remeshed; masked spots smaller than the new polygons are remeshed with the
    // rest; a join that folds gets a wider cut (1, then 2 more rings of the new surface); narrow gaps and notches
    // between masked areas are kept too (a closing of 1, 2, then 4 rings).
    std::vector<uint8_t> keep = keepIn;
    StitchFailure first;
    size_t pinchFixes = 0;
    int rings = 0, cutRings = 0;
    for (int attempt = 0; attempt < 24; ++attempt) {
        StitchFailure why;
        std::string err;
        if (stitchOnce(source, keep, remeshed, out, err, why, cutRings)) {
            if (note) {
                size_t added = 0, removed = 0;
                for (size_t t = 0; t < nt; ++t) {
                    added += keep[t] && !keepIn[t];
                    removed += !keep[t] && keepIn[t];
                }
                note->clear();
                if (added || removed) {
                    *note = "the mask's edge was tidied to fit the new surface (";
                    if (added) *note += std::to_string(added) + " triangles in narrow gaps kept";
                    if (added && removed) *note += ", ";
                    if (removed) *note += std::to_string(removed) + " triangles at tight corners or small spots remeshed";
                    *note += ")";
                }
            }
            return true;
        }
        if (first.kind == StitchFailure::None) {
            first = why;
            error = err;
        }
        bool changed = false;
        if (why.kind == StitchFailure::KeptPinch && !why.pinches.empty() && pinchFixes < 8) {
            const std::unordered_set<uint32_t> pinch(why.pinches.begin(), why.pinches.end());
            for (size_t t = 0; t < nt; ++t) {
                const glm::uvec3& tri = source.triangles[t];
                if (keep[t] && (pinch.count(tri.x) || pinch.count(tri.y) || pinch.count(tri.z))) {
                    keep[t] = 0;
                    changed = true;
                }
            }
            ++pinchFixes;
        } else if (why.kind == StitchFailure::Unmatched && !why.tooSmall.empty()) {
            dropPieces(source, keep, why.tooSmall);
            changed = true;
        } else if ((why.kind == StitchFailure::Join || why.kind == StitchFailure::Cut) && cutRings < 2) {
            ++cutRings;
            changed = true;
        } else if (why.kind != StitchFailure::OpenEdge && rings < 4) {
            rings = rings ? rings * 2 : 1;
            const std::vector<uint8_t> before = keep;
            closeKept(source, keep, rings);
            changed = keep != before;
        }
        if (!changed) break;
        if (std::find(keep.begin(), keep.end(), 1) == keep.end()) break;
        if (std::find(keep.begin(), keep.end(), 0) == keep.end()) break;
    }
    // Still no clean join: say what to change.
    switch (first.kind) {
    case StitchFailure::KeptPinch:
        error = "The masked area's edge touches itself (" + std::to_string(first.pinches.size()) +
                (first.pinches.size() == 1 ? " place" : " places") +
                "); blur the mask (Tool > Masking > Blur) or grow it by 2 passes, then try again";
        break;
    case StitchFailure::OpenEdge:
        break; // (its own message says what to do)
    case StitchFailure::Unmatched:
        error = "Two masked areas are too close together, one is too small for this resolution, or the mask runs inside "
                "another part of the model; grow the mask by 2-3 passes (Tool > Masking > Grow) to join them, unmask the "
                "small spots, or unmask where the parts overlap";
        break;
    default:
        error = "Couldn't join the masked area to the remeshed surface; blur the mask (Tool > Masking > Blur) so its "
                "edge is smoother, or grow it by 2 passes, then try again";
        break;
    }
    return false;
}

} // namespace sf
