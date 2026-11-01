// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Island packing (UV Editor > Pack, uv_pack): each island turned to its smallest bounding box, scaled to one texel
// density (or kept as it is), and its box packed with a skyline packer into one tile or across UDIM tiles. The
// scale is the largest that fits (found by bisection), or a fixed texel density that spills into as many tiles as
// it needs.
#include "core/Mesh.h"
#include "core/ThreadPool.h"
#include "core/Uv.h"
#include "core/UvInternal.h"

#include <xatlas.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <cstdio>

namespace sf {

using namespace uvdetail;

namespace {

// Skyline of one unit tile: segments [x, x + w) at height y, left to right, covering [border, 1 - border].
struct Skyline {
    struct Segment {
        float x, y, w;
    };
    std::vector<Segment> segs;
    float border = 0.0f;
    explicit Skyline(float b) : border(b) { segs.push_back({b, b, 1.0f - 2.0f * b}); }

    // Lowest place for a w x h box: the top it would reach, and where (false when it doesn't fit).
    bool find(float w, float h, float& bestTop, float& bestX, size_t& bestIndex) const {
        bool found = false;
        const float right = 1.0f - border + 1e-6f, top = 1.0f - border + 1e-6f;
        for (size_t i = 0; i < segs.size(); ++i) {
            const float x = segs[i].x;
            if (x + w > right) break;
            float y = 0.0f, covered = 0.0f;
            for (size_t j = i; j < segs.size() && covered < w; ++j) {
                y = std::max(y, segs[j].y);
                covered = segs[j].x + segs[j].w - x;
            }
            if (covered + 1e-6f < w || y + h > top) continue;
            if (!found || y + h < bestTop - 1e-7f || (y + h <= bestTop + 1e-7f && x < bestX)) {
                found = true;
                bestTop = y + h;
                bestX = x;
                bestIndex = i;
            }
        }
        return found;
    }
    void place(size_t i, float w, float topY) {
        const float x = segs[i].x, end = x + w;
        std::vector<Segment> out;
        out.reserve(segs.size() + 2);
        out.insert(out.end(), segs.begin(), segs.begin() + std::ptrdiff_t(i));
        out.push_back({x, topY, w});
        for (size_t j = i; j < segs.size(); ++j) {
            const float sEnd = segs[j].x + segs[j].w;
            if (sEnd <= end + 1e-7f) continue;
            if (segs[j].x < end) out.push_back({end, segs[j].y, sEnd - end});
            else out.push_back(segs[j]);
        }
        // Merge neighbours at the same height.
        segs.clear();
        for (const Segment& s : out) {
            if (!segs.empty() && std::abs(segs.back().y - s.y) < 1e-7f) segs.back().w = s.x + s.w - segs.back().x;
            else segs.push_back(s);
        }
    }
};

// The shape packer (xatlas, on the islands as a UV mesh): islands are rasterised and fitted by their outlines, so
// far less space goes to waste than with boxes. The islands go in scaled to one density (UV area = surface area,
// or kept relative with k = 1), so xatlas's texels per unit is the texel density itself: with `density` it packs at
// exactly that and opens as many tiles as it needs; otherwise the largest density that fits `tiles` is found by
// bisection at a quarter of the resolution, then packed at full resolution. False when xatlas can't (the box
// packer takes over).
bool packByShape(Mesh& m, const UvTopology& topo, const std::vector<uint32_t>& list, const std::vector<uint32_t>& offsets,
                 const std::vector<uint32_t>& corners, const std::vector<float>& k, const std::vector<float>& angle,
                 const UvPackOptions& o, UvPackResult& result) {
    const int res = std::clamp(o.resolution, 64, 16384);
    std::vector<uint32_t> vid(topo.uvVertexCount, ~0u);
    std::vector<glm::vec2> verts;
    std::vector<uint32_t> idx, cornerOf, material;
    double total = 0.0;
    for (size_t p = 0; p < list.size(); ++p) {
        const uint32_t i = list[p];
        glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        for (uint32_t c = offsets[i]; c < offsets[i + 1]; ++c) lo = glm::min(lo, m.uvs[corners[c]]), hi = glm::max(hi, m.uvs[corners[c]]);
        const glm::vec2 mid = (lo + hi) * 0.5f;
        const float ca = std::cos(angle[p]), sa = std::sin(angle[p]);
        for (uint32_t c = offsets[i]; c < offsets[i + 1]; ++c) {
            const uint32_t corner = corners[c], v = topo.uvVertex[corner];
            if (vid[v] == ~0u) {
                vid[v] = uint32_t(verts.size());
                verts.push_back(rotate(m.uvs[corner] - mid, ca, sa) * k[p]);
            }
            idx.push_back(vid[v]);
            cornerOf.push_back(corner);
            if (idx.size() % 3 == 0) material.push_back(uint32_t(p));
        }
    }
    for (size_t t = 0; t + 2 < idx.size(); t += 3) // (the islands' area as they go in)
        total += 0.5 * std::abs(double(cross2(verts[idx[t + 1]] - verts[idx[t]], verts[idx[t + 2]] - verts[idx[t]])));
    if (idx.empty() || total <= 0.0) return false;
    std::unique_ptr<xatlas::Atlas, void (*)(xatlas::Atlas*)> atlas(xatlas::Create(), xatlas::Destroy);
    xatlas::UvMeshDecl decl;
    decl.vertexUvData = verts.data();
    decl.vertexStride = sizeof(glm::vec2);
    decl.vertexCount = uint32_t(verts.size());
    decl.indexData = idx.data();
    decl.indexCount = uint32_t(idx.size());
    decl.indexFormat = xatlas::IndexFormat::UInt32;
    decl.faceMaterialData = material.data(); // (one per island: islands that overlap stay apart)
    if (xatlas::AddUvMesh(atlas.get(), decl) != xatlas::AddMeshError::Success) return false;
    xatlas::ComputeCharts(atlas.get());
    auto pack = [&](float texelsPerUnit, int resolution) {
        xatlas::PackOptions po;
        po.padding = uint32_t(std::lround(std::max(o.marginPx, 0.0f) * float(resolution) / float(res)));
        po.texelsPerUnit = texelsPerUnit;
        po.resolution = uint32_t(resolution);
        po.bilinear = false;
        po.blockAlign = resolution > 1024;
        po.rotateCharts = o.rotate; // (quarter turns, undone exactly by the fit below)
        po.rotateChartsToAxis = false; // (turned to their smallest box above: xatlas's own turn could mirror)
        xatlas::PackCharts(atlas.get(), po);
        return atlas->meshCount == 1 ? int(std::max(atlas->atlasCount, 1u)) : 0;
    };
    const int tiles = std::clamp(o.tiles, 1, 100);
    float density = o.density;
    int used = 0;
    if (density > 0.0f) {
        used = pack(density, res);
        if (used == 0 || used > 100) return false;
    } else {
        // Bisection at a quarter of the resolution (fast), then the full pack, a little smaller until it fits.
        const int lowRes = std::max(res / 4, 256);
        const float rel = float(lowRes) / float(res);
        float lo = 0.0f, hi = float(std::sqrt(double(tiles) * double(res) * double(res) / total));
        for (int it = 0; it < 12; ++it) {
            const float mid = lo > 0.0f ? 0.5f * (lo + hi) : hi * 0.6f;
            const int count = pack(mid * rel, lowRes);
            if (count > 0 && count <= tiles) lo = mid;
            else hi = mid;
            if (lo > 0.0f && hi / lo < 1.01f) break;
        }
        if (lo <= 0.0f) return false;
        density = lo;
        for (int tries = 0; tries < 8; ++tries, density *= 0.97f)
            if ((used = pack(density, res)) > 0 && used <= tiles) break;
        if (used == 0 || used > tiles) return false;
    }
    const xatlas::Mesh& out = atlas->meshes[0];
    if (out.indexCount != idx.size()) return false;
    const int first = std::max(o.firstTile, 1001) - 1001;
    const float inv = 1.0f / float(res);
    // Only where xatlas put each island is taken: the move (and scale) that best maps the island onto its packed
    // copy is applied to the island's own UVs, so its shape stays exactly as it was.
    // xatlas may split an island it finds folded or pinched into several charts: then the islands wouldn't stay
    // whole, and the box packer takes over.
    for (size_t i = 3; i < idx.size(); ++i)
        if (material[i / 3] == material[(i - 1) / 3] &&
            out.vertexArray[out.indexArray[i]].chartIndex != out.vertexArray[out.indexArray[i - 1]].chartIndex)
            return false;
    std::vector<glm::vec2> placed(out.indexCount);
    for (uint32_t i = 0; i < out.indexCount; ++i) {
        const xatlas::Vertex& v = out.vertexArray[out.indexArray[i]];
        const int t = first + std::max(v.atlasIndex, 0);
        placed[i] = glm::vec2(float(t % 10), float(t / 10)) + glm::vec2(v.uv[0], v.uv[1]) * inv;
    }
    for (size_t b = 0; b < idx.size();) {
        size_t e = b;
        while (e < idx.size() && material[e / 3] == material[b / 3]) e += 3;
        glm::dvec2 ca(0.0), cb(0.0);
        for (size_t i = b; i < e; ++i) ca += glm::dvec2(verts[idx[i]]), cb += glm::dvec2(placed[i]);
        ca /= double(e - b), cb /= double(e - b);
        double dotSum = 0.0, crossSum = 0.0, norm = 0.0;
        for (size_t i = b; i < e; ++i) {
            const glm::dvec2 p = glm::dvec2(verts[idx[i]]) - ca, q = glm::dvec2(placed[i]) - cb;
            dotSum += p.x * q.x + p.y * q.y;
            crossSum += p.x * q.y - p.y * q.x;
            norm += p.x * p.x + p.y * p.y;
        }
        const double s = norm > 0.0 ? std::sqrt(dotSum * dotSum + crossSum * crossSum) / norm : 1.0;
        const double a = std::atan2(crossSum, dotSum), c = std::cos(a) * s, sn = std::sin(a) * s;
        for (size_t i = b; i < e; ++i) {
            const glm::dvec2 p = glm::dvec2(verts[idx[i]]) - ca;
            m.uvs[cornerOf[i]] = glm::vec2(cb + glm::dvec2(c * p.x - sn * p.y, sn * p.x + c * p.y));
        }
        b = e;
    }
    const double uvArea = total;
    result.islands = list.size();
    result.tilesUsed = used;
    // (UV area after packing = surface area x (density / res)^2 when the islands went in at one density.)
    result.coverage = float(uvArea * double(density) * double(density) * double(inv) * double(inv) / double(std::max(used, 1)));
    result.density = uvMeshDensity(m, res);
    m.facesDirty = true;
    return true;
}

} // namespace

int packRectangles(const std::vector<glm::vec2>& sizes, int maxTiles, bool allowTurn, float border,
                   std::vector<glm::vec2>& positions, std::vector<uint8_t>& turned, std::vector<int>& tile) {
    const size_t n = sizes.size();
    positions.assign(n, glm::vec2(0.0f));
    turned.assign(n, 0);
    tile.assign(n, 0);
    if (n == 0) return 1;
    std::vector<uint32_t> order(n);
    std::iota(order.begin(), order.end(), 0u);
    // Tallest first (by the longer side when turning is allowed).
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        const float ka = allowTurn ? std::max(sizes[a].x, sizes[a].y) : sizes[a].y;
        const float kb = allowTurn ? std::max(sizes[b].x, sizes[b].y) : sizes[b].y;
        return ka > kb || (ka == kb && a < b);
    });
    std::vector<Skyline> tiles;
    tiles.emplace_back(border);
    for (uint32_t r : order) {
        glm::vec2 s = sizes[r];
        // With turning, boxes lie down (wide) first: that suits a skyline.
        bool turn = allowTurn && s.y > s.x;
        bool placed = false;
        for (size_t t = 0; !placed; ++t) {
            if (t == tiles.size()) {
                if (int(t) >= maxTiles) return 0;
                tiles.emplace_back(border);
            }
            float bestTop = 0, bestX = 0;
            size_t bestI = 0;
            bool any = false, bestTurn = turn;
            for (int k = 0; k < (allowTurn ? 2 : 1); ++k) {
                const bool tr = k == 0 ? turn : !turn;
                const glm::vec2 d = tr ? glm::vec2(s.y, s.x) : s;
                float top = 0, x = 0;
                size_t i = 0;
                if (tiles[t].find(d.x, d.y, top, x, i) && (!any || top < bestTop - 1e-7f)) {
                    any = true;
                    bestTop = top, bestX = x, bestI = i, bestTurn = tr;
                }
            }
            if (!any) {
                if (t + 1 == tiles.size() && tiles[t].segs.size() == 1 && tiles[t].segs[0].y == border) return 0; // too big
                continue;
            }
            const glm::vec2 d = bestTurn ? glm::vec2(s.y, s.x) : s;
            positions[r] = glm::vec2(bestX, bestTop - d.y);
            turned[r] = bestTurn;
            tile[r] = int(t);
            tiles[t].place(bestI, d.x, bestTop);
            placed = true;
        }
    }
    return int(tiles.size());
}

bool uvPack(Mesh& m, const UvTopology& topo, const std::vector<uint8_t>& islands, const UvPackOptions& o,
            UvPackResult& result, std::string& error) {
    result = UvPackResult();
    const size_t nt = m.triangleCount();
    if (m.uvs.size() != nt * 3 || topo.island.size() != nt) {
        error = "The Part has no UVs to pack";
        return false;
    }
    std::vector<uint32_t> list;
    for (uint32_t i = 0; i < topo.islandCount; ++i)
        if (islands.empty() || (i < islands.size() && islands[i])) list.push_back(i);
    if (list.empty()) {
        error = "No islands to pack";
        return false;
    }
    std::vector<uint32_t> offsets, corners;
    islandCorners(topo, offsets, corners);
    const int res = std::clamp(o.resolution, 64, 16384);
    const size_t n = list.size();
    struct Piece {
        float angle = 0.0f, k = 1.0f;
        glm::vec2 lo{0.0f}, size{0.0f}; // rotated, before scaling by k
        double area3 = 0.0, areaUv = 0.0;
    };
    std::vector<Piece> pieces(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t p = b; p < e; ++p) {
            const uint32_t i = list[p];
            Piece& pc = pieces[p];
            for (uint32_t k = topo.islandOffsets[i]; k < topo.islandOffsets[i + 1]; ++k) {
                const uint32_t t = topo.islandTriangles[k];
                const glm::uvec3& tri = m.triangles[t];
                pc.area3 += 0.5 * double(glm::length(glm::cross(m.positions[tri.y] - m.positions[tri.x], m.positions[tri.z] - m.positions[tri.x])));
                const glm::vec2* u = m.uvs.data() + size_t(t) * 3;
                pc.areaUv += 0.5 * std::abs(double(cross2(u[1] - u[0], u[2] - u[0])));
            }
            std::vector<glm::vec2> pts;
            pts.reserve(offsets[i + 1] - offsets[i]);
            for (uint32_t c = offsets[i]; c < offsets[i + 1]; ++c) pts.push_back(m.uvs[corners[c]]);
            pc.angle = o.rotate ? minAreaRectAngle(pts) : 0.0f;
            const float ca = std::cos(pc.angle), sa = std::sin(pc.angle);
            glm::vec2 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
            for (const glm::vec2& q : pts) {
                const glm::vec2 r = rotate(q, ca, sa);
                lo = glm::min(lo, r);
                hi = glm::max(hi, r);
            }
            pc.lo = lo;
            pc.size = glm::max(hi - lo, glm::vec2(0.0f));
            const bool equal = o.equalDensity || o.density > 0.0f;
            pc.k = equal && pc.areaUv > 1e-20 && pc.area3 > 0.0 ? float(std::sqrt(pc.area3 / pc.areaUv)) : 1.0f;
        }
    }, 16);
    // The shape packer when there are enough islands for their shapes to matter (and not so many triangles that
    // rasterising them would keep you waiting); the box packer always runs too, and the denser layout wins.
    std::vector<glm::vec2> shapeUvs;
    UvPackResult shapeResult;
    if (n >= 8 && m.triangleCount() <= 600000) {
        std::vector<float> k(n), angle(n);
        std::vector<double> a3(n);
        for (size_t p = 0; p < n; ++p) k[p] = pieces[p].k, angle[p] = pieces[p].angle, a3[p] = pieces[p].area3;
        const std::vector<glm::vec2> before = m.uvs;
        if (packByShape(m, topo, list, offsets, corners, k, angle, o, shapeResult)) shapeUvs = m.uvs;
        m.uvs = before;
    }
    auto useShapes = [&] {
        if (shapeUvs.empty()) return false;
        m.uvs = std::move(shapeUvs);
        result = shapeResult;
        m.facesDirty = true;
        return true;
    };
    const float margin = std::max(o.marginPx, 0.0f) / float(res);
    std::vector<glm::vec2> sizes(n), pos;
    std::vector<uint8_t> turned;
    std::vector<int> tileOf;
    auto tryScale = [&](float s, int maxTiles) {
        for (size_t p = 0; p < n; ++p) sizes[p] = pieces[p].size * pieces[p].k * s + glm::vec2(margin);
        return packRectangles(sizes, maxTiles, o.rotate, 0.0f, pos, turned, tileOf);
    };
    float scale = 1.0f;
    int used = 0;
    if (o.density > 0.0f) {
        scale = o.density / float(res); // UV units per surface unit
        used = tryScale(scale, 100);
        if (!used) {
            if (useShapes()) return true;
            error = "At that texel density an island is bigger than a tile (or it needs over 100 tiles): lower it";
            return false;
        }
    } else {
        const int tiles = std::clamp(o.tiles, 1, 100);
        double boxes = 0.0;
        for (const Piece& pc : pieces) boxes += double(pc.size.x * pc.k) * double(pc.size.y * pc.k);
        if (boxes <= 0.0) {
            error = "The islands have no area to pack";
            return false;
        }
        float hi = float(std::sqrt(double(tiles) / boxes)), lo = 0.0f;
        // The largest scale that fits, by bisection (lo always fits once found).
        float fits = 0.0f;
        for (int it = 0; it < 30 && fits == 0.0f; ++it) {
            const float s = hi * std::pow(0.7f, float(it));
            if (tryScale(s, tiles)) fits = s;
        }
        if (fits == 0.0f) {
            if (useShapes()) return true;
            error = "The islands don't fit: lower the margin or use more tiles";
            return false;
        }
        lo = fits;
        hi = std::min(hi, fits / 0.7f);
        for (int it = 0; it < 14; ++it) {
            const float mid = 0.5f * (lo + hi);
            if (tryScale(mid, tiles)) lo = mid;
            else hi = mid;
        }
        scale = lo;
        used = tryScale(scale, tiles);
    }
    // Place: rotate, scale, move into the box (inset by half the margin), then into its tile.
    const int columns = 10;
    const int first = std::max(o.firstTile, 1001) - 1001;
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t p = b; p < e; ++p) {
            const uint32_t i = list[p];
            const Piece& pc = pieces[p];
            const float ca = std::cos(pc.angle), sa = std::sin(pc.angle), k = pc.k * scale;
            const int t = first + tileOf[p];
            const glm::vec2 origin(float(t % columns), float(t / columns));
            const glm::vec2 box = pc.size * k;
            for (uint32_t c = offsets[i]; c < offsets[i + 1]; ++c) {
                glm::vec2 q = (rotate(m.uvs[corners[c]], ca, sa) - pc.lo) * k;
                if (turned[p]) q = glm::vec2(box.y - q.y, q.x); // a quarter turn, staying in the box
                m.uvs[corners[c]] = origin + pos[p] + glm::vec2(margin * 0.5f) + q;
            }
        }
    }, 16);
    double uvArea = 0.0;
    for (size_t p = 0; p < n; ++p) uvArea += pieces[p].areaUv * double(pieces[p].k * scale) * double(pieces[p].k * scale);
    result.islands = n;
    result.tilesUsed = used;
    result.coverage = float(uvArea / double(std::max(used, 1)));
    result.density = uvMeshDensity(m, res);
    m.facesDirty = true;
    // Fixed density: the layout that needs fewer tiles; fitting tiles: the denser one.
    if (!shapeUvs.empty() && (o.density > 0.0f ? shapeResult.tilesUsed < used : shapeResult.density > result.density * 1.005f))
        useShapes();
    return true;
}

} // namespace sf
