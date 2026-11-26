// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Surface nets with one vertex per connected piece of the surface inside a cell (Voxel.cpp and VoxelSparse.cpp share it).
// A cell whose corners are inside on a diagonal (a 1-3 voxel slit seen end on, two walls touching along a line) used to get
// ONE vertex for both walls, so the mesh pinched there: an edge with 4 faces. Here the crossings of a cell are joined by the
// cube's faces: on a face with two crossings they are one piece; on a face with four (a diagonal) each inside corner keeps
// its own piece, so the two walls are two sheets that touch. The rule reads signs only, and a face is shared by two cells,
// so both cells decide it alike and the surface stays manifold. A quad's four corners are the pieces holding its grid edge.
// (Joining the inside across a diagonal instead leaves the walls welded and a crease's far end as a sealed air tube; the
// loose solid bits this rule leaves along a groove's rim are dropped by dropSpecks in Voxel.cpp.)
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace sf {

struct SurfaceNetsCells {
    // Per sign configuration (bit k = corner k inside; k = x | y << 1 | z << 2): the piece of each edge (a corner without the
    // axis bit, times 3, plus the axis; -1 where the edge has no crossing) and how many pieces there are.
    std::vector<std::array<int8_t, 24>> piece;
    std::vector<uint8_t> count;

    static int edgeId(int lowCorner, int axis) { return lowCorner * 3 + axis; }

    // The table index of a cell: its signs.
    static int key(const std::array<float, 8>& c, float iso) {
        int cfg = 0;
        for (int k = 0; k < 8; ++k) cfg |= int(c[size_t(k)] > iso) << k;
        return cfg;
    }

    SurfaceNetsCells() : piece(256), count(256) {
        for (int cfg = 0; cfg < 256; ++cfg) {
            std::array<int, 24> parent;
            for (int i = 0; i < 24; ++i) parent[size_t(i)] = i;
            auto find = [&](int i) {
                while (parent[size_t(i)] != i) i = parent[size_t(i)] = parent[size_t(parent[size_t(i)])];
                return i;
            };
            auto inside = [&](int corner) { return (cfg >> corner & 1) != 0; };
            auto crossing = [&](int a, int b) { return inside(a) != inside(b); };
            auto edgeOf = [&](int a, int b) {
                const int lo = a < b ? a : b, hi = a < b ? b : a;
                int axis = 0;
                while ((hi ^ lo) != (1 << axis)) ++axis;
                return edgeId(lo, axis);
            };
            for (int axis = 0; axis < 3; ++axis) {
                const int u = (axis + 1) % 3, v = (axis + 2) % 3;
                for (int side = 0; side < 2; ++side) {
                    const int ring[4] = {side << axis, side << axis | 1 << u, side << axis | 1 << u | 1 << v,
                                         side << axis | 1 << v};
                    int edges[4], n = 0;
                    for (int i = 0; i < 4; ++i) {
                        const int a = ring[i], b = ring[(i + 1) % 4];
                        edges[i] = crossing(a, b) ? edgeOf(a, b) : -1;
                        n += edges[i] >= 0;
                    }
                    if (n == 2) {
                        int first = -1;
                        for (int i = 0; i < 4; ++i)
                            if (edges[i] >= 0) {
                                if (first < 0) first = edges[i];
                                else parent[size_t(find(first))] = find(edges[i]);
                            }
                    } else if (n == 4) {
                        for (int i = 0; i < 4; ++i)
                            if (inside(ring[i])) parent[size_t(find(edges[(i + 3) % 4]))] = find(edges[i]);
                    }
                }
            }
            std::array<int, 24> number;
            number.fill(-1);
            int pieces = 0;
            for (int a = 0; a < 8; ++a)
                for (int axis = 0; axis < 3; ++axis) {
                    int8_t& out = piece[size_t(cfg)][size_t(edgeId(a, axis))];
                    out = -1;
                    if (a >> axis & 1) continue;
                    if (!crossing(a, a | 1 << axis)) continue;
                    int& n = number[size_t(find(edgeId(a, axis)))];
                    if (n < 0) n = pieces++;
                    out = int8_t(n);
                }
            count[size_t(cfg)] = uint8_t(pieces);
        }
    }

    static const SurfaceNetsCells& get() {
        static const SurfaceNetsCells t;
        return t;
    }
};

} // namespace sf
