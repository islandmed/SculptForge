// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Polygon mode's Edge loop op; see core/PolyEdgeLoop.h.
#include "core/PolyEdgeLoop.h"

#include "core/EdgeLoopInsert.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>

namespace sf {

namespace {

constexpr int kMaxCount = 20;

std::string count(size_t n, const char* what) { return std::to_string(n) + " " + what + (n == 1 ? "" : "s"); }

// The edge of `topo` between vertices x and y, or kNoEdge.
uint32_t edgeBetween(const EdgeTopology& topo, uint32_t x, uint32_t y) {
    if (x >= topo.vertexOffsets.size() - 1) return kNoEdge;
    for (uint32_t i = topo.vertexOffsets[x]; i < topo.vertexOffsets[x + 1]; ++i) {
        const uint32_t e = topo.vertexEdges[i];
        if (topo.ends[e].x == y || topo.ends[e].y == y) return e;
    }
    return kNoEdge;
}

uint64_t pairKey(uint32_t a, uint32_t b) { return uint64_t(std::min(a, b)) << 32 | std::max(a, b); }

} // namespace

bool polyEdgeLoop(const PolyOpInput& in, PolyOpOutput& out, std::string& error) {
    // Edges only: a face or point selection converts to far more edges than meant (a band of faces is three rings).
    if (in.element != PolyElement::Edges || in.edgePairs.empty()) {
        error = "Edge loop needs an edge: in Edges, click one (or select a ring with Ring) first";
        return false;
    }
    EdgeLoopOptions o;
    o.count = int(std::lround(std::clamp(argNumber(in.args, "count", 1.0), 1.0, double(kMaxCount))));
    o.slide = float(std::clamp(argNumber(in.args, "position", 0.5), 0.0, 1.0) * 2.0 - 1.0);

    out.mesh = in.mesh;
    out.element = PolyElement::Edges;
    out.edges.clear();
    // Each selected edge seeds a ring unless an earlier ring already covers it. The first ring uses the input's
    // topology; every further one needs the topology of the mesh as cut so far (the old vertices keep their ids, so
    // the remaining seeds are found again by their vertex pairs).
    std::unordered_set<uint64_t> covered;
    std::shared_ptr<const EdgeTopology> rebuilt;
    const EdgeTopology* topo = &in.topo;
    size_t rings = 0, edges = 0, quads = 0, fans = 0;
    bool closed = false;
    for (const glm::uvec2& pr : in.edgePairs) {
        if (covered.count(pairKey(pr.x, pr.y))) continue;
        if (rings > 0) {
            rebuilt = buildEdgeTopology(out.mesh);
            topo = rebuilt.get();
        }
        const uint32_t seed = edgeBetween(*topo, pr.x, pr.y);
        if (seed == kNoEdge) continue;
        const EdgeRing ring = edgeRing(out.mesh, *topo, seed);
        if (ring.empty()) {
            covered.insert(pairKey(pr.x, pr.y));
            continue;
        }
        for (size_t k = 0; k < ring.edges.size(); ++k) covered.insert(pairKey(ring.left[k], ring.right[k]));
        EdgeLoopResult r;
        if (!insertEdgeLoops(out.mesh, *topo, ring, o, r, error)) return false;
        // A loop made before may run through this ring's quads: its edges on the ring are split now, so the result
        // selection names their pieces instead.
        if (rings > 0 && !out.edges.empty()) {
            std::unordered_set<uint64_t> onRing;
            for (size_t k = 0; k < ring.edges.size(); ++k) onRing.insert(pairKey(ring.left[k], ring.right[k]));
            std::vector<glm::uvec2> kept;
            for (const glm::uvec2& pr : out.edges)
                if (!onRing.count(pairKey(pr.x, pr.y))) kept.push_back(pr);
            for (size_t k = 0; k < ring.edges.size(); ++k) {
                if (!std::any_of(out.edges.begin(), out.edges.end(), [&](const glm::uvec2& pr) {
                        return pairKey(pr.x, pr.y) == pairKey(ring.left[k], ring.right[k]);
                    }))
                    continue;
                uint32_t prev = ring.left[k];
                for (const std::vector<uint32_t>& loop : r.loops) kept.emplace_back(prev, loop[k]), prev = loop[k];
                kept.emplace_back(prev, ring.right[k]);
            }
            out.edges.swap(kept);
        }
        for (const std::vector<uint32_t>& loop : r.loops) {
            const size_t n = loop.size();
            for (size_t k = 0; k + 1 < n; ++k) out.edges.emplace_back(loop[k], loop[k + 1]);
            if (r.closed && n > 2) out.edges.emplace_back(loop[n - 1], loop[0]);
        }
        ++rings;
        if (rings == 1) {
            edges = ring.edges.size();
            closed = r.closed;
        }
        quads += r.quadsSplit;
        fans += r.polygonsFanned;
    }
    if (rings == 0) {
        error = "Edge loop: no ring of quads runs through the selected edges (an edge shared by three faces cannot be split)";
        return false;
    }
    if (rings == 1)
        out.message = "Edge loop: " + count(size_t(o.count), "loop") + " of " + count(edges, "edge") + (closed ? " round" : "") +
                      ", " + count(quads, "quad") + " split" + (fans ? ", ended on " + count(fans, "triangle") : "");
    else
        out.message = "Edge loops: " + count(size_t(o.count), "loop") + " on each of " + count(rings, "ring") + ", " +
                      count(quads, "quad") + " split";
    return true;
}

void registerPolyEdgeLoopOp(PolyOpRegistry& reg) {
    PolyOp op;
    op.id = "edge_loop";
    op.label = "Edge loop";
    op.description = "Insert new loops of edges across the ring of quads through the selected edge (or every ring the selected "
                     "edges are on): Count loops per ring, evenly spaced, at Position across the quads (0.5 = centred). The "
                     "loop runs round the model or stops at a border, a triangle (split in two) or a quad it already crossed; "
                     "face groups, UVs, seams, creases, paint and mask are kept. One undo step; the new loops stay selected.";
    op.credit = "#225 and the ideas threads";
    op.needs = PolyElement::Edges;
    op.preview = true;
    op.params = {{"count", "Loops", 1.0, double(kMaxCount), 1.0, "How many loops to insert across each ring of quads, evenly spaced."},
                 {"position", "Position", 0.0, 1.0, 0.5,
                  "Where across the quads the loops go: 0.5 is centred, towards 0 or 1 they move close to either side of the ring."}};
    op.run = polyEdgeLoop;
    reg.add(std::move(op));
}

} // namespace sf
