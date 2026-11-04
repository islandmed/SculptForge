// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Voxel.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "core/ThreadPool.h"

namespace sf {

namespace {

// Running max (dilate) or min (erode) over a box of +-radius cells, one axis at a time (separable). Rows along x,
// whole rows for y and whole slices for z, so every pass reads memory in order; the slices run in parallel.
void morph(std::vector<uint8_t>& data, const glm::ivec3& dims, int radius, bool dilate) {
    if (radius <= 0) return;
    const size_t X = size_t(dims.x), Y = size_t(dims.y), Z = size_t(dims.z), S = X * Y;
    std::vector<uint8_t> tmp(data.size());
    auto combine = [dilate](uint8_t* out, const uint8_t* in, size_t count) {
        for (size_t i = 0; i < count; ++i) out[i] = dilate ? std::max(out[i], in[i]) : std::min(out[i], in[i]);
    };
    poolFor(Y * Z, [&](size_t b, size_t e) { // x: data -> tmp
        for (size_t row = b; row < e; ++row) {
            const uint8_t* in = &data[row * X];
            uint8_t* out = &tmp[row * X];
            for (int x = 0; x < dims.x; ++x) {
                uint8_t r = in[x];
                for (int k = std::max(0, x - radius); k <= std::min(dims.x - 1, x + radius); ++k)
                    r = dilate ? std::max(r, in[k]) : std::min(r, in[k]);
                out[x] = r;
            }
        }
    }, 64);
    poolFor(Z, [&](size_t b, size_t e) { // y: tmp -> data
        for (size_t z = b; z < e; ++z)
            for (int y = 0; y < dims.y; ++y) {
                uint8_t* out = &data[z * S + size_t(y) * X];
                std::copy_n(&tmp[z * S + size_t(y) * X], X, out);
                for (int k = std::max(0, y - radius); k <= std::min(dims.y - 1, y + radius); ++k)
                    if (k != y) combine(out, &tmp[z * S + size_t(k) * X], X);
            }
    }, 1);
    poolFor(Z, [&](size_t b, size_t e) { // z: data -> tmp
        for (size_t z = b; z < e; ++z) {
            std::copy_n(&data[z * S], S, &tmp[z * S]);
            for (int k = std::max(0, int(z) - radius); k <= std::min(dims.z - 1, int(z) + radius); ++k)
                if (k != int(z)) combine(&tmp[z * S], &data[size_t(k) * S], S);
        }
    }, 1);
    data.swap(tmp);
}

// Labels 6-connected regions where data[i] == value, starting from `seeds` (already marked in `reached`). Span fill:
// each popped sample claims its whole x run, and only the first sample of each run beside it is pushed.
void floodFill(const std::vector<uint8_t>& data, uint8_t value, const glm::ivec3& dims, std::vector<uint32_t>& stack,
               std::vector<uint8_t>& reached) {
    const size_t X = size_t(dims.x), S = X * size_t(dims.y);
    auto open = [&](size_t j) { return !reached[j] && data[j] == value; };
    while (!stack.empty()) {
        const size_t i = stack.back();
        stack.pop_back();
        const size_t row = i - i % X; // the row's first sample
        size_t lo = i, hi = i;
        while (lo > row && open(lo - 1)) reached[--lo] = 1;
        while (hi + 1 < row + X && open(hi + 1)) reached[++hi] = 1;
        const size_t y = (i / X) % size_t(dims.y), z = i / S;
        auto scan = [&](bool inRange, size_t offset, bool up) {
            if (!inRange) return;
            bool inRun = false;
            for (size_t k = lo; k <= hi; ++k) {
                const size_t j = up ? k + offset : k - offset;
                const bool o = open(j);
                if (o && !inRun) {
                    reached[j] = 1;
                    stack.push_back(uint32_t(j));
                }
                inRun = o;
            }
        };
        scan(y > 0, X, false);
        scan(y + 1 < size_t(dims.y), X, true);
        scan(z > 0, S, false);
        scan(z + 1 < size_t(dims.z), S, true);
    }
}

void blurAxis(std::vector<float>& f, const glm::ivec3& dims, int axis) {
    static const std::array<float, 5> w = [] {
        std::array<float, 5> k{};
        float sum = 0.0f;
        for (int i = 0; i < 5; ++i) sum += k[size_t(i)] = std::exp(-0.5f * float((i - 2) * (i - 2)));
        for (float& x : k) x /= sum;
        return k;
    }();
    const glm::ivec3 step(1, dims.x, dims.x * dims.y);
    const int n = dims[axis];
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    std::vector<float> line(static_cast<size_t>(n));
    for (int b = 0; b < dims[v]; ++b)
        for (int a = 0; a < dims[u]; ++a) {
            const size_t start = size_t(a) * size_t(step[u]) + size_t(b) * size_t(step[v]);
            for (int i = 0; i < n; ++i) line[size_t(i)] = f[start + size_t(i) * size_t(step[axis])];
            for (int i = 0; i < n; ++i) {
                float s = 0.0f;
                for (int k = -2; k <= 2; ++k) s += w[size_t(k + 2)] * line[size_t(std::clamp(i + k, 0, n - 1))];
                f[start + size_t(i) * size_t(step[axis])] = s;
            }
        }
}

// Closest point to `p` on triangle abc (Ericson, Real-Time Collision Detection 5.1.5).
glm::vec3 closestOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

} // namespace

bool makeVoxelGrid(const AABB& box, int resolution, int padding, VoxelGrid& grid, size_t maxCells) {
    if (!box.valid()) return false;
    resolution = std::max(resolution, 4);
    const glm::vec3 ext = glm::max(box.extent(), glm::vec3(1e-6f));
    const float cell = std::max({ext.x, ext.y, ext.z}) / float(resolution);
    glm::ivec3 dims = glm::ivec3(glm::ceil(ext / cell)) + 1 + 2 * padding;
    if (double(dims.x) * dims.y * dims.z > double(maxCells)) return false;
    grid.dims = dims;
    grid.cell = cell;
    grid.origin = box.center() - glm::vec3(dims - 1) * 0.5f * cell;
    grid.values.assign(grid.size(), 0.0f);
    return true;
}

std::vector<uint8_t> solidifyShell(const VoxelGrid& shell, float threshold, int closeRadius, bool keepAllParts) {
    const glm::ivec3 dims = shell.dims;
    const size_t n = shell.size();
    std::vector<uint8_t> wall(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) wall[i] = shell.values[i] > threshold ? 1 : 0;
    }, 1 << 16);
    morph(wall, dims, closeRadius, true);

    // Flood the empty space from the grid border; whatever it can't reach is inside.
    std::vector<uint8_t> outside(n, 0);
    std::vector<uint32_t> stack;
    for (int z = 0; z < dims.z; ++z)
        for (int y = 0; y < dims.y; ++y)
            for (int x = 0; x < dims.x; ++x) {
                const bool border = x == 0 || y == 0 || z == 0 || x == dims.x - 1 || y == dims.y - 1 || z == dims.z - 1;
                const size_t i = shell.index(x, y, z);
                if (border && !wall[i] && !outside[i]) {
                    outside[i] = 1;
                    stack.push_back(uint32_t(i));
                }
            }
    floodFill(wall, 0, dims, stack, outside);

    std::vector<uint8_t> solid(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) solid[i] = outside[i] ? 0 : 1;
    }, 1 << 16);
    morph(solid, dims, closeRadius, false); // undo the gap-closing dilation

    if (!keepAllParts) {
        // Keep the largest connected piece (captures are full of floaters).
        std::vector<uint32_t> label(n, 0);
        const int64_t sy = dims.x, sz = int64_t(dims.x) * dims.y;
        uint32_t current = 0, bestLabel = 0;
        size_t bestCount = 0;
        for (size_t seed = 0; seed < n; ++seed) {
            if (!solid[seed] || label[seed]) continue;
            label[seed] = ++current;
            stack.assign(1, uint32_t(seed));
            size_t count = 0;
            while (!stack.empty()) {
                const uint32_t i = stack.back();
                stack.pop_back();
                ++count;
                const int x = int(i % uint32_t(dims.x));
                const int y = int((i / uint32_t(dims.x)) % uint32_t(dims.y));
                const int z = int(i / uint32_t(sz));
                const int64_t offsets[6] = {-1, 1, -sy, sy, -sz, sz};
                const bool valid[6] = {x > 0, x + 1 < dims.x, y > 0, y + 1 < dims.y, z > 0, z + 1 < dims.z};
                for (int k = 0; k < 6; ++k) {
                    if (!valid[k]) continue;
                    const uint32_t j = uint32_t(int64_t(i) + offsets[k]);
                    if (solid[j] && !label[j]) {
                        label[j] = current;
                        stack.push_back(j);
                    }
                }
            }
            if (count > bestCount) {
                bestCount = count;
                bestLabel = current;
            }
        }
        for (size_t i = 0; i < n; ++i) solid[i] = label[i] == bestLabel && bestLabel ? 1 : 0;
    }
    return solid;
}

Mesh meshFromSolid(const VoxelGrid& grid, const std::vector<uint8_t>& solid, int smoothIterations) {
    std::vector<float> f(solid.begin(), solid.end());
    for (int axis = 0; axis < 3; ++axis) blurAxis(f, grid.dims, axis);
    return meshFromField(grid, f, 0.5f, smoothIterations);
}

Mesh meshFromField(const VoxelGrid& grid, const std::vector<float>& f, float iso, int smoothIterations) {
    const glm::ivec3 dims = grid.dims;
    const glm::ivec3 cells = dims - 1;
    if (glm::any(glm::lessThan(cells, glm::ivec3(1)))) return {};
    const size_t cellSlice = size_t(cells.x) * size_t(cells.y);
    std::vector<int32_t> cellVertex(cellSlice * size_t(cells.z), -1);
    auto cellIndex = [&](int x, int y, int z) { return (size_t(z) * size_t(cells.y) + size_t(y)) * size_t(cells.x) + size_t(x); };

    // One vertex per cell that the surface passes through (surface nets). Slices run in parallel, each numbering its
    // own vertices; the slices' counts then give the global numbers, in the same z, y, x order as a serial scan.
    std::vector<std::vector<glm::vec3>> slicePositions(size_t(cells.z));
    poolFor(size_t(cells.z), [&](size_t zb, size_t ze) {
        for (int z = int(zb); z < int(ze); ++z) {
            std::vector<glm::vec3>& out = slicePositions[size_t(z)];
            for (int y = 0; y < cells.y; ++y)
                for (int x = 0; x < cells.x; ++x) {
                    std::array<float, 8> c;
                    int inside = 0;
                    for (int k = 0; k < 8; ++k) {
                        c[size_t(k)] = f[grid.index(x + (k & 1), y + ((k >> 1) & 1), z + ((k >> 2) & 1))];
                        inside += c[size_t(k)] > iso;
                    }
                    if (inside == 0 || inside == 8) continue;
                    glm::vec3 sum(0.0f);
                    int crossings = 0;
                    for (int a = 0; a < 8; ++a)
                        for (int bit = 1; bit < 8; bit <<= 1) {
                            if (a & bit) continue;
                            const int b = a | bit;
                            const float fa = c[size_t(a)], fb = c[size_t(b)];
                            if ((fa > iso) == (fb > iso)) continue;
                            const float t = (iso - fa) / (fb - fa);
                            const glm::vec3 pa(a & 1, (a >> 1) & 1, (a >> 2) & 1);
                            const glm::vec3 pb(b & 1, (b >> 1) & 1, (b >> 2) & 1);
                            sum += glm::mix(pa, pb, t);
                            ++crossings;
                        }
                    cellVertex[cellIndex(x, y, z)] = int32_t(out.size());
                    out.push_back(grid.position(x, y, z) + sum / float(crossings) * grid.cell);
                }
        }
    }, 1);
    std::vector<size_t> firstVertex(size_t(cells.z) + 1, 0);
    for (size_t z = 0; z < size_t(cells.z); ++z) firstVertex[z + 1] = firstVertex[z] + slicePositions[z].size();
    Mesh m;
    m.positions.resize(firstVertex.back());
    poolFor(size_t(cells.z), [&](size_t zb, size_t ze) {
        for (size_t z = zb; z < ze; ++z) {
            std::copy(slicePositions[z].begin(), slicePositions[z].end(), m.positions.begin() + ptrdiff_t(firstVertex[z]));
            std::vector<glm::vec3>().swap(slicePositions[z]);
            if (firstVertex[z] == 0) continue;
            for (size_t i = z * cellSlice; i < (z + 1) * cellSlice; ++i)
                if (cellVertex[i] >= 0) cellVertex[i] += int32_t(firstVertex[z]);
        }
    }, 1);

    // One quad per grid edge with a sign change, joining the four cells around it; slices in parallel, joined in order.
    std::vector<std::vector<glm::uvec3>> sliceTriangles(size_t(dims.z));
    poolFor(size_t(dims.z), [&](size_t zb, size_t ze) {
        for (int z = int(zb); z < int(ze); ++z) {
            std::vector<glm::uvec3>& out = sliceTriangles[size_t(z)];
            for (int y = 0; y < dims.y; ++y)
                for (int x = 0; x < dims.x; ++x) {
                    const glm::ivec3 p(x, y, z);
                    const bool in0 = f[grid.index(x, y, z)] > iso;
                    for (int axis = 0; axis < 3; ++axis) {
                        glm::ivec3 q = p;
                        q[axis] += 1;
                        if (q[axis] >= dims[axis]) continue;
                        const bool in1 = f[grid.index(q.x, q.y, q.z)] > iso;
                        if (in0 == in1) continue;
                        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                        if (p[u] < 1 || p[v] < 1 || p[u] > cells[u] - 1 || p[v] > cells[v] - 1 || p[axis] > cells[axis] - 1)
                            continue;
                        std::array<int32_t, 4> quad;
                        const int du[4] = {-1, 0, 0, -1}, dv[4] = {-1, -1, 0, 0};
                        bool ok = true;
                        for (int k = 0; k < 4; ++k) {
                            glm::ivec3 c = p;
                            c[u] += du[k];
                            c[v] += dv[k];
                            quad[size_t(k)] = cellVertex[cellIndex(c.x, c.y, c.z)];
                            ok &= quad[size_t(k)] >= 0;
                        }
                        if (!ok) continue;
                        if (!in0) std::swap(quad[1], quad[3]); // outward normal must point to the outside sample
                        const auto& P = m.positions;
                        const bool diag02 = glm::distance(P[size_t(quad[0])], P[size_t(quad[2])]) <=
                                            glm::distance(P[size_t(quad[1])], P[size_t(quad[3])]);
                        const glm::uvec4 qq(quad[0], quad[1], quad[2], quad[3]);
                        if (diag02) {
                            out.push_back({qq.x, qq.y, qq.z});
                            out.push_back({qq.x, qq.z, qq.w});
                        } else {
                            out.push_back({qq.x, qq.y, qq.w});
                            out.push_back({qq.y, qq.z, qq.w});
                        }
                    }
                }
        }
    }, 1);
    size_t triangleTotal = 0;
    for (const auto& t : sliceTriangles) triangleTotal += t.size();
    m.triangles.reserve(triangleTotal);
    for (auto& t : sliceTriangles) {
        m.triangles.insert(m.triangles.end(), t.begin(), t.end());
        std::vector<glm::uvec3>().swap(t);
    }
    // Surface nets makes quads: keep each one (Mesh::quadPartners): an even all-quad surface.
    for (uint32_t t = 0; t + 1 < m.triangles.size(); t += 2) m.pairQuad(t, t + 1);
    m.rebuildTopology();
    taubinSmooth(m, smoothIterations);
    m.recomputeAllNormals();
    return m;
}

void rasterizeMeshShell(const Mesh& mesh, VoxelGrid& grid) {
    const auto& P = mesh.positions;
    for (const auto& t : mesh.triangles) {
        const glm::vec3 a = (P[t.x] - grid.origin) / grid.cell;
        const glm::vec3 b = (P[t.y] - grid.origin) / grid.cell;
        const glm::vec3 c = (P[t.z] - grid.origin) / grid.cell;
        const float longest = std::max({glm::distance(a, b), glm::distance(b, c), glm::distance(c, a)});
        const int steps = std::max(1, int(std::ceil(longest * 2.0f)));
        for (int i = 0; i <= steps; ++i)
            for (int j = 0; i + j <= steps; ++j) {
                const float u = float(i) / float(steps), v = float(j) / float(steps);
                const glm::ivec3 s(glm::round(a + (b - a) * u + (c - a) * v));
                if (glm::any(glm::lessThan(s, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(s, grid.dims))) continue;
                grid.values[grid.index(s.x, s.y, s.z)] = 1.0f;
            }
    }
}

std::vector<float> insideDistanceField(const Mesh& mesh, const VoxelGrid& grid, const std::vector<uint8_t>& solid, float band) {
    // Exact distance (in cells) to the nearest triangle for every sample within `band` cells of the surface, found by
    // visiting the samples around each triangle (a narrow band: nothing else is touched). Triangles are binned into
    // z-slabs that run in parallel, each writing only its own samples and visiting its triangles in index order, so
    // ties go to the same triangle as a serial pass.
    const glm::ivec3 dims = grid.dims;
    const size_t n = grid.size();
    std::vector<float> dist2(n, band * band);
    std::vector<int32_t> nearest(n, -1);
    const auto& P = mesh.positions;
    const float inv = 1.0f / grid.cell;
    const uint32_t triangles = uint32_t(mesh.triangleCount());
    auto local = [&](uint32_t t, glm::vec3& a, glm::vec3& b, glm::vec3& c, glm::ivec3& lo, glm::ivec3& hi) {
        const glm::uvec3 tri = mesh.triangles[t];
        a = (P[tri.x] - grid.origin) * inv, b = (P[tri.y] - grid.origin) * inv, c = (P[tri.z] - grid.origin) * inv;
        lo = glm::max(glm::ivec3(glm::floor(glm::min(a, glm::min(b, c)) - band)), glm::ivec3(0));
        hi = glm::min(glm::ivec3(glm::ceil(glm::max(a, glm::max(b, c)) + band)), dims - 1);
    };
    constexpr int kSlab = 4;
    const size_t slabs = size_t((dims.z + kSlab - 1) / kSlab);
    std::vector<glm::ivec2> slabRange(triangles);
    poolFor(triangles, [&](size_t b0, size_t e0) {
        for (size_t t = b0; t < e0; ++t) {
            glm::vec3 a, b, c;
            glm::ivec3 lo, hi;
            local(uint32_t(t), a, b, c, lo, hi);
            slabRange[t] = lo.z <= hi.z ? glm::ivec2(lo.z / kSlab, hi.z / kSlab) : glm::ivec2(1, 0); // (NaN: none)
        }
    }, 1 << 16);
    std::vector<uint32_t> slabStart(slabs + 1, 0);
    for (const glm::ivec2& r : slabRange)
        for (int s = r.x; s <= r.y; ++s) ++slabStart[size_t(s) + 1];
    for (size_t s = 0; s < slabs; ++s) slabStart[s + 1] += slabStart[s];
    std::vector<uint32_t> slabTriangles(slabStart.back());
    {
        std::vector<uint32_t> cursor(slabStart.begin(), slabStart.end() - 1);
        for (uint32_t t = 0; t < triangles; ++t)
            for (int s = slabRange[t].x; s <= slabRange[t].y; ++s) slabTriangles[cursor[size_t(s)]++] = t;
    }
    std::vector<glm::ivec2>().swap(slabRange);
    poolFor(slabs, [&](size_t sb, size_t se) {
        for (size_t s = sb; s < se; ++s) {
            const int z0 = int(s) * kSlab, z1 = std::min(dims.z, z0 + kSlab);
            for (uint32_t k = slabStart[s]; k < slabStart[s + 1]; ++k) {
                const uint32_t t = slabTriangles[k];
                glm::vec3 a, b, c;
                glm::ivec3 lo, hi;
                local(t, a, b, c, lo, hi);
                // Only samples within band of the triangle's bounding sphere can be within band of the triangle.
                const glm::vec3 centre = (a + b + c) / 3.0f;
                const float radius = std::sqrt(std::max({glm::dot(a - centre, a - centre), glm::dot(b - centre, b - centre),
                                                         glm::dot(c - centre, c - centre)})) + 1e-3f;
                const float reach = radius + band;
                const float reach2 = reach * reach;
                for (int z = std::max(lo.z, z0); z <= std::min(hi.z, z1 - 1); ++z) {
                    const float dz = float(z) - centre.z, rz = reach2 - dz * dz;
                    if (rz < 0.0f) continue;
                    for (int y = lo.y; y <= hi.y; ++y) {
                        const float dy = float(y) - centre.y, ry = rz - dy * dy;
                        if (ry < 0.0f) continue;
                        const float half = std::sqrt(ry);
                        const int x0 = std::max(lo.x, int(std::ceil(centre.x - half)));
                        const int x1 = std::min(hi.x, int(std::floor(centre.x + half)));
                        for (int x = x0; x <= x1; ++x) {
                            const glm::vec3 p(x, y, z);
                            const size_t i = grid.index(x, y, z);
                            // Skip when even the sphere's near side is no closer than the best so far (most samples
                            // once a slab's first triangles are in: small triangles overlap many times).
                            const float far = std::sqrt(dist2[i]) + radius;
                            if (glm::dot(p - centre, p - centre) > far * far) continue;
                            const glm::vec3 d = p - closestOnTriangle(p, a, b, c);
                            const float d2 = glm::dot(d, d);
                            if (d2 < dist2[i]) {
                                dist2[i] = d2;
                                nearest[i] = int32_t(t);
                            }
                        }
                    }
                }
            }
        }
    }, 1);
    std::vector<uint32_t>().swap(slabTriangles);

    // Sign: the flood fill is certain about the outside and about the deep inside; next to the surface the
    // nearest triangle's side decides, which keeps sub-voxel detail instead of voxel stair-steps.
    std::vector<uint8_t> nearOutside(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) nearOutside[i] = solid[i] ? 0 : 1;
    }, 1 << 16);
    morph(nearOutside, dims, 2, true); // within 2 samples (any axis) of an outside sample
    std::vector<uint8_t> inside(n);
    poolFor(size_t(dims.z), [&](size_t zb, size_t ze) {
        for (int z = int(zb); z < int(ze); ++z)
            for (int y = 0; y < dims.y; ++y)
                for (int x = 0; x < dims.x; ++x) {
                    const size_t i = grid.index(x, y, z);
                    if (!solid[i]) inside[i] = 0;
                    else if (!nearOutside[i] || nearest[i] < 0) inside[i] = 1;
                    else {
                        const glm::uvec3 tri = mesh.triangles[size_t(nearest[i])];
                        const glm::vec3 nrm = glm::cross(P[tri.y] - P[tri.x], P[tri.z] - P[tri.x]);
                        const glm::vec3 p = grid.position(x, y, z);
                        const glm::vec3 onSurface = closestOnTriangle(p, P[tri.x], P[tri.y], P[tri.z]);
                        // A surface buried inside another part (solid just outside it too) doesn't bound the solid.
                        const float len = glm::length(nrm);
                        const glm::ivec3 probe(glm::round((onSurface + nrm * (2.5f * grid.cell / std::max(len, 1e-20f)) -
                                                           grid.origin) / grid.cell));
                        const bool buried = len > 0.0f && glm::all(glm::greaterThanEqual(probe, glm::ivec3(0))) &&
                                            glm::all(glm::lessThan(probe, dims)) && solid[grid.index(probe.x, probe.y, probe.z)];
                        inside[i] = buried || glm::dot(p - onSurface, nrm) < 0.0f;
                    }
                }
    }, 1);
    std::vector<uint8_t>().swap(nearOutside);
    // Surfaces buried inside the solid (overlapping parts) make small "outside" pockets: anything outside that
    // can't reach the real outside is inside, so merged parts stay one closed surface. The flood starts from the
    // outside samples that touch an outside-signed solid sample (the rest of the outside has nowhere to go).
    std::vector<uint8_t> reached(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) reached[i] = solid[i] ? 0 : 1;
    }, 1 << 16);
    const size_t X = size_t(dims.x), S = X * size_t(dims.y);
    std::vector<std::vector<uint32_t>> sliceSeeds(size_t(dims.z));
    poolFor(size_t(dims.z), [&](size_t zb, size_t ze) {
        for (int z = int(zb); z < int(ze); ++z)
            for (int y = 0; y < dims.y; ++y)
                for (int x = 0; x < dims.x; ++x) {
                    const size_t i = grid.index(x, y, z);
                    if (solid[i]) continue;
                    auto pocket = [&](bool inRange, size_t j) { return inRange && solid[j] && !inside[j]; };
                    if (pocket(x > 0, i - 1) || pocket(x + 1 < dims.x, i + 1) || pocket(y > 0, i - X) ||
                        pocket(y + 1 < dims.y, i + X) || pocket(z > 0, i - S) || pocket(z + 1 < dims.z, i + S))
                        sliceSeeds[size_t(z)].push_back(uint32_t(i));
                }
    }, 1);
    std::vector<uint32_t> stack;
    for (const auto& seeds : sliceSeeds) stack.insert(stack.end(), seeds.begin(), seeds.end());
    floodFill(inside, 0, dims, stack, reached);
    std::vector<float> f(n);
    poolFor(n, [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const float d = std::sqrt(dist2[i]);
            f[i] = inside[i] || !reached[i] ? d : -d;
        }
    }, 1 << 16);
    return f;
}

Mesh voxelRemesh(const Mesh& mesh, int resolution, std::string* error, const std::function<bool(float)>& progress) {
    VoxelGrid grid;
    if (mesh.triangleCount() == 0 || !makeVoxelGrid(mesh.bounds(), resolution, 4, grid)) {
        if (error) *error = mesh.triangleCount() == 0 ? "The mesh is empty" : "Resolution too high for this mesh";
        return {};
    }
    auto step = [&](float f) {
        if (!progress || progress(f)) return true;
        if (error) *error = "Voxel Remesh cancelled";
        return false;
    };
    rasterizeMeshShell(mesh, grid);
    if (!step(0.1f)) return {};
    std::vector<uint8_t> solid = solidifyShell(grid, 0.5f, 1, true);
    std::vector<float>().swap(grid.values);
    if (!step(0.25f)) return {};
    // Mesh the signed distance to the original surface (positive inside), not the blocky occupancy: the result
    // lands on the original surface with no voxel terracing.
    const std::vector<float> field = insideDistanceField(mesh, grid, solid, 3.0f);
    std::vector<uint8_t>().swap(solid);
    if (!step(0.6f)) return {};
    Mesh out = meshFromField(grid, field, 0.0f, 2);
    if (out.triangleCount() == 0 && error) *error = "Remesh produced no surface";
    if (!step(0.9f)) return {};
    // Mask and vertex paint follow the nearest original vertex; face groups, hidden
    // flags and UVs don't survive a rebuild (everything becomes visible group 0).
    // The original's quads don't either: the new surface is all quads of its own.
    transferVertexAttributes(mesh, out);
    step(1.0f);
    return out;
}

} // namespace sf
