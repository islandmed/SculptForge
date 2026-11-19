// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Masking.h"

#include "core/Commands.h"
#include "core/Micro.h"
#include "core/MicroHistory.h"
#include "core/VertexPaint.h"
#include "core/ScreenSelect.h"
#include "core/Sculpt.h"
#include "core/ThreadPool.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sf {

namespace {

// Applies `next` (old mask -> new value of vertex v) to every visible vertex,
// double-buffered so each pass reads the previous one.
template <class F>
bool maskPass(Mesh& mesh, F next) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    std::vector<float>& mask = mesh.ensureMask();
    std::vector<float> out(mask);
    const bool anyHidden = mesh.hasHidden();
    bool changed = false;
    for (uint32_t v = 0; v < n; ++v) {
        if (anyHidden && mesh.vertexHidden(v)) continue;
        const float m = std::clamp(next(mask, v), 0.0f, 1.0f);
        if (m != mask[v]) {
            out[v] = m;
            changed = true;
        }
    }
    if (changed) {
        mask.swap(out);
        mesh.markAllDirty();
    }
    return changed;
}

float neighbourAverage(const Mesh& mesh, const std::vector<float>& mask, uint32_t v) {
    auto ring = mesh.neighbors(v);
    if (ring.empty()) return mask[v];
    float sum = 0.0f;
    for (uint32_t u : ring) sum += mask[u];
    return sum / static_cast<float>(ring.size());
}

template <class F>
bool repeatPass(Mesh& mesh, int iterations, F next) {
    bool changed = false;
    for (int i = 0; i < std::max(1, iterations); ++i) {
        if (!maskPass(mesh, next)) break;
        changed = true;
    }
    return changed;
}


float smoothstep01(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / std::max(e1 - e0, 1e-6f), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// --- A micro level's own mask (MicroMesh::mask, one value per grid point: what its brushes and the renderer use) ---

enum class MicroMaskOp { Clear, All, Invert, Blur, Sharpen, Grow, Shrink };

// One neighbourhood pass over every visible tile: each point from its neighbours inside its tile, then the copies of a
// point on a shared edge or corner combined (averaged; Grow the largest, Shrink the smallest), as a stroke stitches.
void microMaskPass(const Mesh& m, MicroMesh& mm, MicroMaskOp op, std::vector<uint8_t>& out) {
    const uint32_t n = mm.n, S = mm.samples;
    const std::vector<uint8_t>& in = mm.mask;
    out = in;
    poolFor(mm.baseTriangles, [&](size_t b, size_t e) {
        for (size_t t = b; t < e; ++t) {
            if (m.triangleHidden(uint32_t(t))) continue;
            const uint8_t* g = in.data() + t * S;
            uint8_t* o = out.data() + t * S;
            for (uint32_t j = 0; j <= n; ++j)
                for (uint32_t i = 0; i + j <= n; ++i) {
                    const float v = float(g[MicroMesh::index(i, j, n)]);
                    float sum = 0.0f, lo = v, hi = v;
                    int count = 0;
                    auto at = [&](int di, int dj) {
                        const int a = int(i) + di, c = int(j) + dj;
                        if (a < 0 || c < 0 || uint32_t(a + c) > n) return;
                        const float u = float(g[MicroMesh::index(uint32_t(a), uint32_t(c), n)]);
                        sum += u, lo = std::min(lo, u), hi = std::max(hi, u), ++count;
                    };
                    at(1, 0), at(-1, 0), at(0, 1), at(0, -1), at(1, -1), at(-1, 1);
                    const float avg = count ? sum / float(count) : v;
                    float r = v;
                    if (op == MicroMaskOp::Blur) r = 0.5f * v + 0.5f * avg;
                    else if (op == MicroMaskOp::Sharpen) r = ((v + (v - avg)) - 127.5f) * 1.25f + 127.5f;
                    else if (op == MicroMaskOp::Grow) r = hi;
                    else if (op == MicroMaskOp::Shrink) r = lo;
                    o[MicroMesh::index(i, j, n)] = uint8_t(std::clamp(std::lround(r), 0L, 255L));
                }
        }
    }, 64);
    auto combine = [&](int a, int c) {
        return op == MicroMaskOp::Grow ? std::max(a, c) : op == MicroMaskOp::Shrink ? std::min(a, c) : (a + c + 1) / 2;
    };
    poolFor(mm.baseTriangles, [&](size_t b, size_t e) { // (each shared edge once, by the lower visible triangle)
        for (size_t t = b; t < e; ++t) {
            if (m.triangleHidden(uint32_t(t))) continue;
            const glm::uvec3 tt = m.triangles[t];
            uint8_t* g = out.data() + t * S;
            for (int ed = 0; ed < 3; ++ed) {
                const uint32_t nb = mm.edgeNeighbor[t * 3 + size_t(ed)];
                if (nb == MicroMesh::kNone) continue;
                const uint32_t u = nb >> 2;
                const int f = int(nb & 3);
                const bool hiddenU = m.triangleHidden(u);
                if (!hiddenU && u < t) continue;
                uint8_t* h = out.data() + size_t(u) * S;
                const bool reversed = m.triangles[u][f] != tt[ed];
                for (uint32_t k = 1; k < n; ++k) {
                    uint32_t i, j, i2, j2;
                    MicroMesh::edgePoint(ed, k, n, i, j);
                    MicroMesh::edgePoint(f, reversed ? n - k : k, n, i2, j2);
                    uint8_t& x = g[MicroMesh::index(i, j, n)];
                    uint8_t& y = h[MicroMesh::index(i2, j2, n)];
                    x = hiddenU ? y : (y = uint8_t(combine(x, y)));
                }
            }
        }
    }, 64);
    auto corner = [&](uint32_t t, uint32_t v) -> uint8_t& {
        const glm::uvec3 tt = m.triangles[t];
        const int c = tt.x == v ? 0 : tt.y == v ? 1 : 2;
        return out[size_t(t) * S + (c == 0 ? 0 : c == 1 ? MicroMesh::index(n, 0, n) : MicroMesh::index(0, n, n))];
    };
    poolFor(m.vertexCount(), [&](size_t b, size_t e) {
        for (size_t v = b; v < e; ++v) {
            const IndexRing ring = m.vertexTriangles(uint32_t(v));
            int value = -1, held = -1;
            for (uint32_t t : ring) {
                const int x = corner(t, uint32_t(v));
                if (m.triangleHidden(t)) held = x;
                else value = value < 0 ? x : combine(value, x);
            }
            if (held >= 0) value = held; // (hidden triangles keep theirs)
            if (value >= 0)
                for (uint32_t t : ring) corner(t, uint32_t(v)) = uint8_t(value);
        }
    }, 4096);
}

// Applies `op` to the micro level's mask (`iterations` passes of the neighbourhood ops, each about one base edge
// ring as on the base mesh) and returns the undo step, or null when nothing changed. Kept strokes it reaches are
// fixed in place (the mask is part of what their replay starts from).
std::shared_ptr<MicroUndo> microMaskAdjust(Mesh& mesh, MicroMaskOp op, int iterations) {
    if (!mesh.micro || !microMatches(mesh)) return nullptr;
    const bool neighbourhood = op == MicroMaskOp::Blur || op == MicroMaskOp::Sharpen || op == MicroMaskOp::Grow || op == MicroMaskOp::Shrink;
    if (!mesh.micro->hasMask() && (neighbourhood || op == MicroMaskOp::Clear)) return nullptr;
    MicroMesh& mm = mutableMicro(mesh);
    mm.ensureTopology(mesh);
    const size_t S = mm.samples;
    const bool added = !mm.hasMask();
    if (added) mm.mask.assign(mm.disp.size(), 0);
    std::vector<uint8_t> old = mm.mask;
    if (op == MicroMaskOp::Clear) {
        std::fill(mm.mask.begin(), mm.mask.end(), uint8_t(0));
    } else if (op == MicroMaskOp::All || op == MicroMaskOp::Invert) {
        poolFor(mm.baseTriangles, [&](size_t b, size_t e) {
            for (size_t t = b; t < e; ++t) {
                if (mesh.triangleHidden(uint32_t(t))) continue;
                uint8_t* g = mm.mask.data() + t * S;
                for (size_t k = 0; k < S; ++k) g[k] = op == MicroMaskOp::All ? uint8_t(255) : uint8_t(255 - g[k]);
            }
        }, 64);
    } else {
        // (the base mesh's pass reaches one edge ring; the grid has n points along a base edge. Sharpen is mostly a
        // contrast step: once per iteration, as on the base.)
        const int per = op == MicroMaskOp::Sharpen ? 1 : int(std::min<uint32_t>(mm.n, 16));
        std::vector<uint8_t> next;
        for (int i = 0; i < std::max(1, iterations) * per; ++i) {
            microMaskPass(mesh, mm, op, next);
            if (next == mm.mask) break;
            mm.mask.swap(next);
        }
    }
    auto u = std::make_shared<MicroUndo>();
    u->step = mm.step, u->samples = mm.samples, u->maskAdded = added;
    for (uint32_t t = 0; t < mm.baseTriangles; ++t)
        if (!std::equal(old.begin() + std::ptrdiff_t(t * S), old.begin() + std::ptrdiff_t((t + 1) * S), mm.mask.begin() + std::ptrdiff_t(t * S)))
            u->tris.push_back(t);
    const bool cleared = op == MicroMaskOp::Clear;
    if (u->tris.empty()) {
        if (added) std::vector<uint8_t>().swap(mm.mask);
        return nullptr;
    }
    u->mask.resize(u->tris.size() * S);
    for (size_t k = 0; k < u->tris.size(); ++k) std::copy_n(old.data() + size_t(u->tris[k]) * S, S, u->mask.data() + k * S);
    if (cleared) { // (freed, as on the base mesh: undo makes it again, MicroUndo::maskAdded)
        std::vector<uint8_t>().swap(mm.mask);
        u->maskAdded = true;
    }
    if (added || cleared) mm.maskDirty = true;
    for (uint32_t t : u->tris) mm.markDirty(t);
    ++mm.revision;
    if (MicroHistory* h = microHistory(mesh, false)) h->bakeMeeting(u->tris);
    return u;
}

} // namespace

bool symmetryArg(const Args& a, const Mesh& mesh, Symmetry& sym, std::string& error) {
    sym = Symmetry{false, false, false};
    for (char c : argString(a, "symmetry")) {
        const char l = char(std::tolower(static_cast<unsigned char>(c)));
        if (l == 'x') sym.x = true;
        else if (l == 'y') sym.y = true;
        else if (l == 'z') sym.z = true;
        else {
            error = "symmetry takes the axes x, y and z";
            return false;
        }
    }
    const double radial = argNumber(a, "radial", 1.0);
    if (!(radial >= 1.0 && radial <= double(Symmetry::kMaxRadial)) || radial != std::floor(radial)) {
        error = "radial takes a whole number of copies, 1 (off) to " + std::to_string(Symmetry::kMaxRadial);
        return false;
    }
    sym.radial = int(radial);
    sym.radialEnabled = sym.radial > 1;
    std::string axis = argString(a, "radial_axis");
    if (axis.empty()) axis = "y";
    for (char& c : axis) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (axis != "x" && axis != "y" && axis != "z") {
        error = "radial_axis takes x, y or z";
        return false;
    }
    sym.radialAxis = axis[0] - 'x';
    sym.local = argBool(a, "local", false);
    if (sym.local) sym.pivot = symmetryPivot(mesh);
    return true;
}

bool symmetryCopiesArg(const Args& a, const Mesh& mesh, std::vector<SymCopy>& copies, std::string& error) {
    Symmetry sym;
    if (!symmetryArg(a, mesh, sym, error)) return false;
    copies = Sculptor::copies(sym);
    return true;
}

std::vector<ParamSpec> radialSymmetryParams() {
    return {{"radial", ParamSpec::Type::Number,
             "Radial symmetry: this many copies turned evenly around radial_axis (2-64; default 1, off). With "
             "mirror axes too, each mirror image gets its copies.",
             false},
            {"radial_axis", ParamSpec::Type::String, "The axis radial copies turn around: x, y or z (default y).", false},
            {"local", ParamSpec::Type::Bool,
             "Local symmetry: mirror planes and the radial axis go through the Part's centre instead of the world "
             "origin (default false).",
             false}};
}

bool maskClear(Mesh& mesh) {
    if (!mesh.hasMask()) return false;
    const bool changed = std::any_of(mesh.mask.begin(), mesh.mask.end(), [](float m) { return m != 0.0f; });
    mesh.clearMask();
    mesh.markAllDirty();
    return changed;
}

bool maskAll(Mesh& mesh) {
    return maskPass(mesh, [](const std::vector<float>&, uint32_t) { return 1.0f; });
}

bool maskInvert(Mesh& mesh) {
    return maskPass(mesh, [](const std::vector<float>& m, uint32_t v) { return 1.0f - m[v]; });
}

bool maskBlur(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        return 0.5f * m[v] + 0.5f * neighbourAverage(mesh, m, v);
    });
}

bool maskSharpen(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        // Unsharp step (away from the neighbour average) plus a mild contrast boost around 0.5.
        float s = m[v] + (m[v] - neighbourAverage(mesh, m, v));
        return (s - 0.5f) * 1.25f + 0.5f;
    });
}

bool maskGrow(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        float r = m[v];
        for (uint32_t u : mesh.neighbors(v)) r = std::max(r, m[u]);
        return r;
    });
}

bool maskShrink(Mesh& mesh, int iterations) {
    if (!mesh.hasMask()) return false;
    return repeatPass(mesh, iterations, [&](const std::vector<float>& m, uint32_t v) {
        float r = m[v];
        for (uint32_t u : mesh.neighbors(v)) r = std::min(r, m[u]);
        return r;
    });
}

float vertexConcavity(const Mesh& mesh, uint32_t v) {
    auto ring = mesh.neighbors(v);
    if (ring.empty()) return 0.0f;
    const glm::vec3& p = mesh.positions[v];
    glm::vec3 avg(0.0f);
    float edge = 0.0f;
    for (uint32_t u : ring) {
        avg += mesh.positions[u];
        edge += glm::distance(p, mesh.positions[u]);
    }
    avg /= static_cast<float>(ring.size());
    edge /= static_cast<float>(ring.size());
    if (edge <= 1e-12f) return 0.0f;
    return glm::dot(mesh.normals[v], avg - p) / edge;
}

bool maskByCavity(Mesh& mesh, float strength, float threshold) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    if (n == 0) return false;
    strength = std::clamp(strength, 0.0f, 1.0f);
    // One smoothing pass over the raw concavity keeps single noisy vertices out.
    std::vector<float> raw(n), cav(n);
    for (uint32_t v = 0; v < n; ++v) raw[v] = vertexConcavity(mesh, v);
    for (uint32_t v = 0; v < n; ++v) cav[v] = 0.5f * raw[v] + 0.5f * neighbourAverage(mesh, raw, v);
    constexpr float kRamp = 0.15f; // concavity above the threshold at which the mask is full
    return maskPass(mesh, [&](const std::vector<float>&, uint32_t v) {
        return strength * smoothstep01(threshold, threshold + kRamp, cav[v]);
    });
}

bool maskByPeaks(Mesh& mesh, float strength, float threshold) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    if (n == 0) return false;
    strength = std::clamp(strength, 0.0f, 1.0f);
    std::vector<float> raw(n), cav(n);
    for (uint32_t v = 0; v < n; ++v) raw[v] = vertexConcavity(mesh, v);
    for (uint32_t v = 0; v < n; ++v) cav[v] = 0.5f * raw[v] + 0.5f * neighbourAverage(mesh, raw, v);
    constexpr float kRamp = 0.15f; // (as maskByCavity)
    return maskPass(mesh, [&](const std::vector<float>&, uint32_t v) {
        return strength * smoothstep01(threshold, threshold + kRamp, -cav[v]);
    });
}

bool maskByColor(Mesh& mesh, const glm::vec3& color, float tolerance, float strength, bool add) {
    if (mesh.vertexCount() == 0) return false;
    tolerance = std::clamp(tolerance, 0.001f, 1.0f);
    strength = std::clamp(strength, 0.0f, 1.0f);
    const glm::vec3 c = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f));
    return maskPass(mesh, [&](const std::vector<float>& m, uint32_t v) {
        const float d = glm::distance(mesh.colorAt(v, glm::vec3(1.0f)), c) / 1.7320508f;
        const float w = strength * (1.0f - smoothstep01(tolerance * 0.8f, tolerance, d));
        return add ? std::max(m[v], w) : w;
    });
}

bool maskByFacing(Mesh& mesh, const glm::vec3& direction, float angleDeg, float strength, bool add) {
    if (mesh.vertexCount() == 0 || mesh.normals.size() != mesh.vertexCount()) return false;
    const float len = glm::length(direction);
    if (!(len > 1e-9f)) return false;
    const glm::vec3 dir = direction / len;
    angleDeg = std::clamp(angleDeg, 1.0f, 180.0f);
    strength = std::clamp(strength, 0.0f, 1.0f);
    const float full = std::max(0.0f, angleDeg - 15.0f); // fully masked inside this angle
    return maskPass(mesh, [&](const std::vector<float>& m, uint32_t v) {
        const float a = std::acos(std::clamp(glm::dot(mesh.normals[v], dir), -1.0f, 1.0f)) * 57.29578f;
        const float w = strength * (1.0f - smoothstep01(full, angleDeg, a));
        return add ? std::max(m[v], w) : w;
    });
}

bool maskByFaceGroup(Mesh& mesh, uint32_t group, bool add) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    if (n == 0) return false;
    std::vector<uint8_t> inGroup(n, 0);
    bool found = false;
    for (uint32_t t = 0; t < mesh.triangleCount(); ++t) {
        if (mesh.facegroupAt(t) != group || mesh.triangleHidden(t)) continue;
        const glm::uvec3& tri = mesh.triangles[t];
        inGroup[tri.x] = inGroup[tri.y] = inGroup[tri.z] = 1;
        found = true;
    }
    if (!found && add) return false;
    return maskPass(mesh, [&](const std::vector<float>& m, uint32_t v) {
        return inGroup[v] ? 1.0f : add ? m[v] : 0.0f;
    });
}

std::vector<uint8_t> borderVertices(const Mesh& mesh, bool facegroups) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    std::vector<uint8_t> border(n, 0);
    poolFor(n, [&](size_t b, size_t e) {
        std::vector<uint32_t> corners;
        for (size_t i = b; i < e; ++i) {
            const uint32_t v = uint32_t(i);
            corners.clear();
            bool mixed = false, seenGroup = false;
            uint32_t group = 0;
            for (uint32_t t : mesh.vertexTriangles(v)) {
                const glm::uvec3& tri = mesh.triangles[t];
                for (int k = 0; k < 3; ++k)
                    if (tri[k] != v) corners.push_back(tri[k]);
                if (!facegroups || mesh.triangleHidden(t)) continue;
                const uint32_t g = mesh.facegroupAt(t);
                if (seenGroup && g != group) mixed = true;
                group = g;
                seenGroup = true;
            }
            if (corners.empty()) continue; // a loose vertex is no border
            // Each edge v-u of a closed surface belongs to two of v's triangles, so u is listed twice; once is open.
            std::sort(corners.begin(), corners.end());
            bool open = false;
            for (size_t c = 0; c < corners.size() && !open;) {
                size_t d = c + 1;
                while (d < corners.size() && corners[d] == corners[c]) ++d;
                open = d - c == 1;
                c = d;
            }
            border[v] = (open || mixed) ? 1 : 0;
        }
    });
    return border;
}

bool maskBorder(Mesh& mesh, int rings, bool facegroups, bool soft, float strength, bool add, size_t* found) {
    const uint32_t n = static_cast<uint32_t>(mesh.vertexCount());
    if (found) *found = 0;
    if (n == 0) return false;
    rings = std::clamp(rings, 1, 20);
    strength = std::clamp(strength, 0.0f, 1.0f);
    // Ring index of each vertex from the border (0 = on it), out to rings - 1; -1 beyond.
    const std::vector<uint8_t> border = borderVertices(mesh, facegroups);
    std::vector<int8_t> ring(n, -1);
    std::vector<uint32_t> frontier, next;
    for (uint32_t v = 0; v < n; ++v)
        if (border[v]) {
            ring[v] = 0;
            frontier.push_back(v);
        }
    if (found) *found = frontier.size();
    if (frontier.empty()) return false;
    for (int r = 1; r < rings && !frontier.empty(); ++r) {
        next.clear();
        for (uint32_t v : frontier)
            for (uint32_t u : mesh.neighbors(v))
                if (ring[u] < 0) {
                    ring[u] = int8_t(r);
                    next.push_back(u);
                }
        frontier.swap(next);
    }
    return maskPass(mesh, [&](const std::vector<float>& m, uint32_t v) {
        float value = 0.0f;
        if (ring[v] >= 0) value = strength * (soft ? 1.0f - float(ring[v]) / float(rings) : 1.0f);
        return add ? std::max(m[v], value) : value;
    });
}

namespace {

// Vertex v, or one of its mirror images, projects inside the shape.
bool inShape(const Mesh& mesh, const glm::mat4& viewProj, const ScreenPolygon& shape,
             const std::vector<SymCopy>& mirrors, uint32_t v, const glm::mat4* invViewProj = nullptr) {
    // With `invViewProj` (Front faces only), the point's normal must face the viewer: against the view ray
    // through it, which works for perspective and orthographic views alike.
    auto facing = [&](const glm::vec3& n, glm::vec2 ndc) {
        if (!invViewProj || v >= mesh.normals.size()) return true;
        const glm::vec4 a = *invViewProj * glm::vec4(ndc, -1.0f, 1.0f), b = *invViewProj * glm::vec4(ndc, 1.0f, 1.0f);
        return glm::dot(n, glm::vec3(b) / b.w - glm::vec3(a) / a.w) < 0.0f;
    };
    glm::vec2 ndc;
    const glm::vec3 n = v < mesh.normals.size() ? mesh.normals[v] : glm::vec3(0.0f);
    if (projectToNdc(viewProj, mesh.positions[v], ndc) && shape.contains(ndc) && facing(n, ndc)) return true;
    for (const SymCopy& m : mirrors)
        if (!m.identity() && projectToNdc(viewProj, m.point(mesh.positions[v]), ndc) && shape.contains(ndc) &&
            facing(m.dir(n), ndc))
            return true;
    return false;
}

} // namespace

bool maskInShape(Mesh& mesh, const glm::mat4& viewProj, const ScreenPolygon& shape, float strength, bool unmask,
                 const std::vector<SymCopy>& mirrors, bool frontFacesOnly) {
    if (mesh.vertexCount() == 0 || !shape.valid()) return false;
    const float s = std::clamp(strength, 0.0f, 1.0f);
    const glm::mat4 inv = glm::inverse(viewProj);
    return maskPass(mesh, [&](const std::vector<float>& m, uint32_t v) {
        if (!inShape(mesh, viewProj, shape, mirrors, v, frontFacesOnly ? &inv : nullptr)) return m[v];
        return unmask ? m[v] * (1.0f - s) : m[v] + (1.0f - m[v]) * s;
    });
}

bool shapeCoversMesh(const Mesh& mesh, const glm::mat4& viewProj, const ScreenPolygon& shape,
                     const std::vector<SymCopy>& mirrors) {
    if (!shape.valid()) return false;
    const bool anyHidden = mesh.hasHidden();
    for (uint32_t v = 0; v < uint32_t(mesh.vertexCount()); ++v)
        if (!(anyHidden && mesh.vertexHidden(v)) && inShape(mesh, viewProj, shape, mirrors, v)) return true;
    return false;
}

void Sculptor::dabMask(Mesh& mesh, const BVH& bvh, const DabParams& p, Symmetry sym) {
    if (!active_) return;
    const float s = std::clamp(p.strength, 0.0f, 1.0f) * std::clamp(p.pressure, 0.0f, 1.0f);
    if (s <= 0.0f) return;
    if (microReady(mesh)) { // the micro level's own mask, per grid point (core/MicroSculpt.cpp)
        const double k0 = micro_.findMs + micro_.kernelMs + micro_.stitchMs;
        inkHeader(true, nullptr, 0, 0.0f, 0.0f);
        for (const SymCopy& mirror : copies(sym)) {
            MicroDab d; // (Forge Ink: kept with the stroke, core/MicroHistory.h)
            d.type = BrushType::Standard, d.center = mirror.point(p.center), d.viewDir = mirror.dir(p.viewDir), d.radius = p.radius;
            d.strength = 1.0f, d.invert = p.invert, d.focal = p.focal, d.alphaUp = mirror.dir(p.alphaUp), d.maskStrength = s;
            d.copy = mirror, d.pxPerUnit = p.pxPerUnit;
            micro_.dab(mesh, bvh, d.type, d.center, d.viewDir, d.radius, d.strength, d.invert, d.focal, d.alphaUp, true, s);
            inkDab(d);
        }
        timings.kernelMs += micro_.findMs + micro_.kernelMs + micro_.stitchMs - k0;
        ++timings.dabs;
        return;
    }
    auto clock = std::chrono::steady_clock::now();
    std::vector<float>& mask = mesh.ensureMask();
    for (const SymCopy& mirror : copies(sym)) {
        const glm::vec3 center = mirror.point(p.center), viewDir = mirror.dir(p.viewDir);
        brushRegion(mesh, bvh, center, viewDir, p.radius, p.connectedOnly, p.viewFalloff, verts_, connected_);
        // New values in parallel; recording and writing them in order.
        weights_.resize(verts_.size());
        poolFor(verts_.size(), [&](size_t b, size_t e) {
            for (size_t i = b; i < e; ++i) {
                const uint32_t v = verts_[i];
                weights_[i] = -1.0f; // unchanged
                if (p.frontFacesOnly && glm::dot(mesh.normals[v], viewDir) > 0.0f) continue;
                const float a = s * brushFalloff(brushDistance(mesh.positions[v], center, viewDir, p.viewFalloff), p.radius, p.focal);
                if (a <= 0.0f) continue;
                const float m = p.invert ? mask[v] * (1.0f - a) : mask[v] + (1.0f - mask[v]) * a;
                if (m != mask[v]) weights_[i] = m;
            }
        });
        for (size_t i = 0; i < verts_.size(); ++i) {
            if (weights_[i] < 0.0f) continue;
            const uint32_t v = verts_[i];
            recordMask(mesh, v);
            mask[v] = weights_[i];
            mesh.markDirty(v);
        }
        timings.vertices += verts_.size();
    }
    timings.kernelMs += lapMs(clock);
    ++timings.dabs;
}

void registerMaskingCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    // Wraps a mask operation as an undoable command.
    auto add = [&registry](std::string name, std::string label, std::string description, std::vector<ParamSpec> params,
                           std::function<bool(Mesh&, const Args&)> op, std::optional<MicroMaskOp> micro = std::nullopt) {
        registry.add({std::move(name), std::move(description), std::move(params),
                      [label, op, micro](Document& doc, const Args& a) -> CommandResult {
                          if (doc.mesh.vertexCount() == 0) return {false, "The mesh is empty"};
                          UndoRecord record = UndoRecord::captureMask(doc.mesh, label);
                          // A micro level brushes and draws with its own mask (per grid point): the op changes that
                          // one, and the base mesh's only when it has one (Invert or All would mask the whole cage).
                          const bool onMicro = micro && doc.mesh.micro;
                          if (onMicro) record.micro = microMaskAdjust(doc.mesh, *micro, std::clamp(int(argNumber(a, "iterations", 1)), 1, 100));
                          const bool base = (!onMicro || doc.mesh.hasMask()) && op(doc.mesh, a);
                          if (!record.micro && !base) return {true, label + ": mask unchanged"};
                          if (!base) record.mask = {};
                          doc.undo.push(std::move(record));
                          doc.modified = true;
                          return {true, label};
                      }});
    };
    const ParamSpec iterations{"iterations", T::Number, "Number of passes (1-100). Default 1.", false};
    auto iters = [](const Args& a) { return std::clamp(int(argNumber(a, "iterations", 1)), 1, 100); };

    add("mask_clear", "Clear Mask", "Remove the mask so every vertex can be sculpted again.", {},
        [](Mesh& m, const Args&) { return maskClear(m); }, MicroMaskOp::Clear);
    add("mask_all", "Mask All", "Mask every visible vertex (protects the whole mesh from brushes).", {},
        [](Mesh& m, const Args&) { return maskAll(m); }, MicroMaskOp::All);
    add("mask_invert", "Invert Mask", "Invert the mask of visible vertices: masked becomes free and vice versa.", {},
        [](Mesh& m, const Args&) { return maskInvert(m); }, MicroMaskOp::Invert);
    add("mask_blur", "Blur Mask", "Soften the mask edges by averaging with neighbouring vertices.", {iterations},
        [iters](Mesh& m, const Args& a) { return maskBlur(m, iters(a)); }, MicroMaskOp::Blur);
    add("mask_sharpen", "Sharpen Mask", "Tighten soft mask edges, pushing values towards fully masked or free.",
        {iterations}, [iters](Mesh& m, const Args& a) { return maskSharpen(m, iters(a)); }, MicroMaskOp::Sharpen);
    add("mask_grow", "Grow Mask", "Expand the masked region by one edge ring per iteration.", {iterations},
        [iters](Mesh& m, const Args& a) { return maskGrow(m, iters(a)); }, MicroMaskOp::Grow);
    add("mask_shrink", "Shrink Mask", "Contract the masked region by one edge ring per iteration.", {iterations},
        [iters](Mesh& m, const Args& a) { return maskShrink(m, iters(a)); }, MicroMaskOp::Shrink);
    add("mask_by_cavity", "Mask by Cavity",
        "Mask concave areas (creases, folds, pores), replacing the current mask - e.g. to protect detail while smoothing.",
        {{"strength", T::Number, "Mask value in the deepest cavities (0-1). Default 1.", false},
         {"threshold", T::Number,
          "Concavity above which vertices get masked (-1 to 1; 0 = anything concave, higher = only deep creases). "
          "Default 0.02.",
          false}},
        [](Mesh& m, const Args& a) {
            return maskByCavity(m, float(argNumber(a, "strength", 1.0)),
                                std::clamp(float(argNumber(a, "threshold", 0.02)), -1.0f, 1.0f));
        });
    add("mask_by_peaks", "Mask by Peaks",
        "Mask convex areas (ridges, peaks, sharp edges), replacing the current mask - e.g. to work only in the "
        "valleys, or to protect edges.",
        {{"strength", T::Number, "Mask value on the sharpest peaks (0-1). Default 1.", false},
         {"threshold", T::Number,
          "Convexity above which vertices get masked (-1 to 1; 0 = anything convex, higher = only sharp ridges). "
          "Default 0.02.",
          false}},
        [](Mesh& m, const Args& a) {
            return maskByPeaks(m, float(argNumber(a, "strength", 1.0)),
                               std::clamp(float(argNumber(a, "threshold", 0.02)), -1.0f, 1.0f));
        });
    add("mask_by_color", "Mask by Colour",
        "Mask the vertices whose vertex paint is close to a colour (unpainted meshes are white).",
        {{"color", T::String, "The colour by name or #rrggbb (or give r, g, b).", false},
         {"r", T::Number, "Red 0-1 (sRGB), instead of color.", false},
         {"g", T::Number, "Green 0-1 (sRGB).", false},
         {"b", T::Number, "Blue 0-1 (sRGB).", false},
         {"tolerance", T::Number, "How different a colour may be and still count (0-1). Default 0.15.", false},
         {"strength", T::Number, "Mask value (0-1). Default 1.", false},
         {"add", T::Bool, "Add to the current mask instead of replacing it. Default false.", false}},
        [](Mesh& m, const Args& a) {
            glm::vec3 c(1.0f);
            std::string err;
            if (!colorArg(a, c, err)) return false;
            return maskByColor(m, c, float(argNumber(a, "tolerance", 0.15)), float(argNumber(a, "strength", 1.0)),
                               argBool(a, "add", false));
        });
    add("mask_by_facing", "Mask by Facing",
        "Mask the vertices whose surface faces a direction (within an angle): e.g. 0 0 1 masks what faces the front "
        "view, 0 1 0 what faces up.",
        {{"x", T::Number, "Direction x.", true},
         {"y", T::Number, "Direction y.", true},
         {"z", T::Number, "Direction z.", true},
         {"angle", T::Number, "Largest angle from the direction, in degrees (1-180). Default 60.", false},
         {"strength", T::Number, "Mask value (0-1). Default 1.", false},
         {"add", T::Bool, "Add to the current mask instead of replacing it. Default false.", false}},
        [](Mesh& m, const Args& a) {
            const glm::vec3 d(float(argNumber(a, "x", 0)), float(argNumber(a, "y", 0)), float(argNumber(a, "z", 1)));
            return maskByFacing(m, d, float(argNumber(a, "angle", 60)), float(argNumber(a, "strength", 1.0)),
                                argBool(a, "add", false));
        });
    add("mask_by_facegroup", "Mask by Face Group",
        "Mask the vertices of one face group (meshes without face groups are all group 0).",
        {{"group", T::Number, "Face Group id.", true},
         {"add", T::Bool, "Add to the current mask instead of replacing it. Default false.", false}},
        [](Mesh& m, const Args& a) {
            return maskByFaceGroup(m, uint32_t(std::max(0.0, argNumber(a, "group", 0))), argBool(a, "add", false));
        });

    registry.add(
        {"mask_border",
         "Mask from the mesh border (Blender: Mask from mesh boundary): mask the vertices on open edges - and, with "
         "face groups, where face groups meet - plus edge rings inside them, so sculpting keeps the border in place. "
         "Replaces the mask unless add is set. A closed mesh has no open edges and masks nothing.",
         {{"rings", T::Number, "Width of the mask in edge rings from the border (1-20; 1 = the border vertices only). "
                               "Default 1.",
           false},
          {"facegroups", T::Bool, "Also mask face group borders. Default false.",
           false},
          {"soft", T::Bool, "Fade the mask from the border towards the innermost ring. Default false.", false},
          {"strength", T::Number, "Mask value on the border (0-1). Default 1.", false},
          {"add", T::Bool, "Add to the current mask instead of replacing it. Default false.", false}},
         [](Document& doc, const Args& a) -> CommandResult {
             if (doc.mesh.vertexCount() == 0) return {false, "The mesh is empty"};
             const std::string label = "Mask Border";
             const bool facegroups = argBool(a, "facegroups", false);
             UndoRecord record = UndoRecord::captureMask(doc.mesh, label);
             size_t found = 0;
             const bool changed =
                 maskBorder(doc.mesh, int(argNumber(a, "rings", 1)), facegroups, argBool(a, "soft", false),
                            float(argNumber(a, "strength", 1.0)), argBool(a, "add", false), &found);
             if (found == 0)
                 return {true, facegroups ? "No open edges or face group borders: the mesh is closed and one face group"
                                          : "No open edges: the mesh is closed"};
             if (!changed) return {true, label + ": mask unchanged"};
             doc.undo.push(std::move(record));
             doc.modified = true;
             return {true, label + ": " + std::to_string(found) + " border vertices"};
         }});

    // MaskRect / MaskLasso. Bad arguments are errors, unlike the "mask unchanged" of a selection that misses.
    const ParamSpec unmask{"unmask", T::Bool, "Unmask the inside instead. Default false.", false};
    const ParamSpec strength{"strength", T::Number, "How much to mask or unmask (0-1). Default 1.", false};
    const ParamSpec viewProj{"view_proj", T::String, "Optional 16 numbers: column-major view-projection matrix.",
                             false};
    const ParamSpec symmetry{"symmetry", T::String,
                             "Mirror axes, e.g. \"x\" or \"xz\": the mirror image of the shape is selected too, as the "
                             "brushes mirror (default none).",
                             false};
    const ParamSpec clearOutside{"clear_outside", T::Bool,
                                 "When the shape covers none of the model, clear the mask instead (like Ctrl+drag on "
                                 "the background). Default false.",
                                 false};
    const ParamSpec frontFaces{"front_faces_only", T::Bool,
                               "Only vertices facing the viewer: the far side of the model, and folds turned away, "
                               "are left as they are (#91.3). Default false.",
                               false};
    auto selection = [&registry](std::string name, std::string description, std::vector<ParamSpec> params,
                                 std::function<bool(const Args&, std::vector<glm::vec2>&, std::string&)> shape) {
        registry.add(
            {std::move(name), std::move(description), std::move(params),
             [shape](Document& doc, const Args& a) -> CommandResult {
                 if (doc.mesh.vertexCount() == 0) return {false, "The mesh is empty"};
                 glm::mat4 vp;
                 std::vector<glm::vec2> points;
                 std::string error;
                 if (!viewProjArg(doc.mesh, a, vp, error) || !shape(a, points, error)) return {false, error};
                 std::vector<SymCopy> mirrors;
                 if (!symmetryCopiesArg(a, doc.mesh, mirrors, error)) return {false, error};
                 const ScreenPolygon polygon(points);
                 const bool unmask = argBool(a, "unmask", false);
                 if (argBool(a, "clear_outside", false) && !shapeCoversMesh(doc.mesh, vp, polygon, mirrors)) {
                     UndoRecord record = UndoRecord::captureMask(doc.mesh, "Clear Mask");
                     if (!maskClear(doc.mesh)) return {true, "Clear Mask: mask unchanged"};
                     doc.undo.push(std::move(record));
                     doc.modified = true;
                     return {true, "Clear Mask"};
                 }
                 const std::string label = unmask ? "Unmask" : "Mask";
                 UndoRecord record = UndoRecord::captureMask(doc.mesh, label);
                 if (!maskInShape(doc.mesh, vp, polygon, float(argNumber(a, "strength", 1.0)), unmask, mirrors,
                                  argBool(a, "front_faces_only", false)))
                     return {true, label + ": mask unchanged"};
                 doc.undo.push(std::move(record));
                 doc.modified = true;
                 return {true, label};
             }});
    };
    selection(
        "mask_rect",
        "Rectangle mask selection: mask the visible vertices inside a screen rectangle, "
        "or unmask them. Coordinates are normalised (-1..1, y up) in the given view; without a view the rectangle "
        "spans the mesh seen from the front (+Z).",
        {{"x0", T::Number, "Rectangle left (-1..1). Default -1.", false},
         {"y0", T::Number, "Rectangle bottom (-1..1). Default -1.", false},
         {"x1", T::Number, "Rectangle right (-1..1). Default 1.", false},
         {"y1", T::Number, "Rectangle top (-1..1). Default 1.", false},
         unmask,
         strength,
         viewProj,
         symmetry,
         clearOutside,
         frontFaces},
        [](const Args& a, std::vector<glm::vec2>& points, std::string&) {
            const float x0 = float(argNumber(a, "x0", -1)), y0 = float(argNumber(a, "y0", -1));
            const float x1 = float(argNumber(a, "x1", 1)), y1 = float(argNumber(a, "y1", 1));
            points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
            return true;
        });
    selection(
        "mask_lasso",
        "Lasso mask selection: mask the visible vertices inside a screen polygon, or "
        "unmask them. Points are normalised (-1..1, y up) in the given view; without a view they span the mesh "
        "seen from the front (+Z).",
        {{"points", T::String, "The lasso: at least 3 points as \"x0 y0 x1 y1 ...\" (-1..1, y up).", true},
         unmask,
         strength,
         viewProj,
         symmetry,
         clearOutside,
         frontFaces},
        lassoArg);
}

} // namespace sf
