// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DetailPass.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Masking.h"
#include "core/Micro.h"
#include "core/PathStroke.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace sf {

namespace {

constexpr float kEdgesPerRadius = 2.5f; // a dab narrower than this many edges only roughens the surface
constexpr int kMaxDabs = 6000;          // one call stays well under a second on a few million triangles

// Sizes are fractions of the Part's bounding-box diagonal: on a head about 25 cm tall, 0.01 is 3-4 mm. Pores, skin
// and scales are round dabs, each pore or scale its own: a patterned alpha can only be turned for the mirror copy,
// not mirrored, so with symmetry the two sides came out different; round dabs mirror exactly, as any stroke.
const std::vector<DetailPreset> kPresets = {
    {"Detail Pores", "pores", BrushType::Standard, "", 0.004f, 1.0f, true, 2.0f,
     "skin pores pressed in (cheeks, nose, forehead): an even field of small pits of mixed sizes"},
    {"Detail Skin", "skin", BrushType::Standard, "", 0.004f, 0.15f, true, 1.5f,
     "fine skin texture: tiny shallow pits and bumps between the pores"},
    {"Detail Wrinkle", "wrinkles", BrushType::Crease, "", 0.004f, 0.5f, false, 0.12f,
     "a thin wrinkle line (around the eyes, the brow, the neck); short strokes"},
    {"Detail Crease", "crease", BrushType::SharpCrease, "", 0.003f, 0.6f, false, 0.1f,
     "a crisp, sharp crease (eyelids, lips, nostrils, folds); one stroke along the line"},
    {"Detail Scales", "scales", BrushType::Standard, "", 0.012f, 0.6f, false, 1.5f,
     "scales in offset rows (reptiles, armoured hide): each a raised dab with its lower rim cut in"},
    {"Detail Scar", "scars", BrushType::Crease, "", 0.005f, 0.9f, false, 0.12f,
     "a scar or cut: a deep line a few centimetres long"},
};

std::string squash(std::string_view s) {
    std::string r;
    for (unsigned char c : s)
        if (c != ' ' && c != '_' && c != '-') r += char(std::tolower(c));
    return r;
}

struct Rng { // xorshift32, as the stroke's jitter: the same seed, the same detail
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float unit() { return float(next() & 0xffffff) / float(0x1000000); } // 0..1
};

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3g", v);
    return buf;
}

std::string vec(const glm::vec3& v) { return num(v.x) + "," + num(v.y) + "," + num(v.z); }

bool lineKind(const std::string& kind) { return kind == "wrinkles" || kind == "scars"; }

// A unit vector on the surface's tangent plane: `want` projected, or (when that's degenerate) horizontal.
glm::vec3 tangentOf(const glm::vec3& n, const glm::vec3& want) {
    glm::vec3 t = want - n * glm::dot(want, n);
    if (glm::length(t) > 1e-4f) return glm::normalize(t);
    t = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), n);
    if (glm::length(t) < 1e-4f) t = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), n);
    return glm::normalize(t);
}

glm::vec3 rotateAbout(const glm::vec3& v, const glm::vec3& axis, float angle) {
    return v * std::cos(angle) + glm::cross(axis, v) * std::sin(angle) + axis * glm::dot(axis, v) * (1.0f - std::cos(angle));
}

} // namespace

const std::vector<DetailPreset>& detailPresets() { return kPresets; }

const DetailPreset* findDetailPreset(std::string_view name) {
    const std::string want = squash(name);
    for (const DetailPreset& p : kPresets)
        if (squash(p.name) == want) return &p;
    return nullptr;
}

std::string detailPresetNames() {
    std::string out;
    for (const DetailPreset& p : kPresets) out += (out.empty() ? "" : ", ") + std::string(p.name);
    return out;
}

float medianEdgeNear(const Mesh& mesh, const glm::vec3& center, float radius) {
    const size_t n = mesh.triangles.size();
    if (n == 0) return 0.0f;
    const size_t stride = std::max<size_t>(1, n / 200000); // (a sample: every triangle of a big mesh isn't needed)
    std::vector<float> edges;
    const float r2 = radius * radius;
    for (size_t i = 0; i < n && edges.size() < 50000; i += stride) {
        const glm::uvec3& t = mesh.triangles[i];
        const glm::vec3 &a = mesh.positions[t.x], &b = mesh.positions[t.y], &c = mesh.positions[t.z];
        if (radius > 0.0f) {
            const float d = std::min({glm::dot(a - center, a - center), glm::dot(b - center, b - center), glm::dot(c - center, c - center)});
            if (d > r2) continue;
        }
        edges.push_back((glm::length(b - a) + glm::length(c - b) + glm::length(a - c)) / 3.0f);
    }
    if (edges.empty()) return 0.0f;
    std::nth_element(edges.begin(), edges.begin() + std::ptrdiff_t(edges.size() / 2), edges.end());
    return edges[edges.size() / 2];
}

int levelsToReach(float edge, float want) {
    int levels = 0;
    while (edge > want * 1.05f && levels < 8) edge *= 0.5f, ++levels;
    return levels;
}

int detailPass(Document& doc, const DetailPassOptions& o, std::string& message, std::string& error,
               std::vector<glm::vec3>* placed) {
    Mesh& mesh = doc.mesh;
    if (mesh.triangleCount() == 0) {
        error = "The active Part is empty";
        return -1;
    }
    const DetailPreset* preset = nullptr;
    for (const DetailPreset& p : kPresets)
        if (o.kind == p.kind && std::string(p.kind) != "crease") preset = &p;
    if (!preset) {
        error = "Unknown kind \"" + o.kind + "\" (pores, skin, wrinkles, scales or scars)";
        return -1;
    }
    const float diag = std::max(glm::length(mesh.bounds().extent()), 1e-6f);
    const float r = (o.size > 0.0f ? std::clamp(o.size, 0.0005f, 0.2f) : preset->size) * diag;
    const float strength = o.strength >= 0.0f ? std::clamp(o.strength, 0.0f, 1.0f) : preset->strength;
    const float density = std::clamp(o.density, 0.02f, 1.0f);
    const bool lines = lineKind(o.kind);

    // Too coarse to hold it: say exactly what would.
    const float edge = medianEdgeNear(mesh, o.center, o.whole ? 0.0f : o.radius + r);
    if (edge <= 0.0f) {
        error = "No surface within " + num(o.radius) + " of " + vec(o.center) + " on the active Part (scene_overview "
                "gives its centre and size)";
        return -1;
    }
    const float smallest = o.kind == "pores" ? 0.75f : o.kind == "skin" ? 0.8f : 1.0f; // (mixed sizes)
    const float holdable = smallest * r / kEdgesPerRadius;
    if (edge > holdable) {
        const int levels = levelsToReach(edge, holdable);
        const double tris = double(mesh.triangleCount()) * std::pow(4.0, levels);
        error = "Too coarse for " + o.kind + " of size " + num(r / diag) + ": edges here are " + num(edge) +
                " (the detail needs " + num(holdable) + " or less). Subdivide first: subdivide levels=" +
                std::to_string(levels) + " (about " + num(tris / 1e6) + "M triangles), or ask for a bigger size=" +
                num(edge * kEdgesPerRadius / diag) + ".";
        return -1;
    }

    doc.syncBvh();

    // The region's triangles, by area (the surface the pass scatters over), and its average facing.
    std::vector<uint32_t> tris;
    std::vector<double> cumulative;
    double area = 0.0;
    glm::vec3 facing(0.0f);
    const float reach2 = o.radius * o.radius;
    for (uint32_t i = 0; i < uint32_t(mesh.triangles.size()); ++i) {
        const glm::uvec3& t = mesh.triangles[i];
        const glm::vec3 &a = mesh.positions[t.x], &b = mesh.positions[t.y], &c = mesh.positions[t.z];
        if (!o.whole) {
            const glm::vec3 m = (a + b + c) / 3.0f;
            if (glm::dot(m - o.center, m - o.center) > reach2) continue;
        }
        const glm::vec3 cross = glm::cross(b - a, c - a);
        area += 0.5 * double(glm::length(cross));
        facing += 0.5f * cross;
        tris.push_back(i);
        cumulative.push_back(area);
    }
    if (tris.empty() || area <= 0.0) {
        error = "No surface in the region";
        return -1;
    }

    // What gets pressed in: stamps (one dab each) or short strokes, each a spot on the surface with its normal, and
    // for strokes a direction on the surface and a length.
    struct Spot {
        glm::vec3 p, n, t{0.0f};
        float length = 0.0f;
        float bend = 0.0f;  // how far a stroke's middle bows sideways (a wrinkle is rarely straight)
        float scale = 1.0f; // this one's radius, times r (pores come in mixed sizes)
        bool flip = false;  // the other way from the kind's (skin: a bump among the pits)
    };
    std::vector<Spot> spots;
    Rng rng(o.seed);
    const glm::vec3 run = glm::length(o.direction) > 1e-6f ? glm::normalize(o.direction) : glm::vec3(0.0f);
    auto runOn = [&](const glm::vec3& n) { // wrinkles run, scales point: `direction`, else horizontal across
        return tangentOf(n, glm::length(run) > 0.0f ? run : glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), n));
    };
    bool capped = false;
    int want = 0;

    if (o.kind == "scales" && !o.whole && glm::length(facing) > 0.0f) {
        // Scales over a region: offset rows (a hex grid) on the region's tangent plane, rows across `direction`,
        // 1.5 radii apart so neighbours overlap; denser packs them a little tighter.
        const glm::vec3 n = glm::normalize(facing), t0 = runOn(n), b = glm::cross(n, t0);
        glm::vec3 c = o.center;
        if (RayHit hit; surfaceRaycast(mesh, doc.bvh, o.center + n * o.radius, -n, hit)) c = hit.position;
        const float step = r * (1.9f - 0.6f * density), rowGap = step * 0.866f;
        int row = 0;
        for (float v = -o.radius; v <= o.radius && !capped; v += rowGap, ++row)
            for (float u = -o.radius + ((row & 1) ? 0.5f * step : 0.0f); u <= o.radius; u += step) {
                if (u * u + v * v > o.radius * o.radius) continue;
                if (int(spots.size()) >= kMaxDabs) {
                    capped = true;
                    break;
                }
                Spot spot{c + t0 * u + b * v, n, t0};
                spot.scale = 0.9f + 0.2f * rng.unit();
                spots.push_back(spot);
            }
        want = int(spots.size());
    } else if (o.kind == "wrinkles" && !o.whole && glm::length(facing) > 0.0f) {
        // Wrinkles over a region: parallel rows across it (a forehead's lines, crow's feet with a direction), each
        // row broken into strokes of 12-20 radii with small gaps, a little wavy, `gap` apart (density: 9 to 3 radii).
        const glm::vec3 n = glm::normalize(facing), t0 = runOn(n), b = glm::cross(n, t0);
        glm::vec3 c = o.center;
        if (RayHit hit; surfaceRaycast(mesh, doc.bvh, o.center + n * o.radius, -n, hit)) c = hit.position;
        const float gap = r * (9.0f - 6.0f * density);
        for (float v = -o.radius + 0.5f * gap; v < o.radius; v += gap) {
            const float chord = std::sqrt(std::max(0.0f, o.radius * o.radius - v * v));
            float u = -chord + rng.unit() * 4.0f * r;
            while (u < chord && int(spots.size()) < kMaxDabs / 120) {
                const float length = r * (12.0f + 8.0f * rng.unit());
                const float mid = std::min(u + 0.5f * length, chord - 0.25f * length);
                const float wobble = (rng.unit() - 0.5f) * 0.35f * gap;
                spots.push_back({c + t0 * mid + b * (v + wobble), n, rotateAbout(t0, n, (rng.unit() - 0.5f) * 0.2f),
                                 std::min(length, 2.0f * chord), (rng.unit() - 0.5f) * 3.0f * r});
                u += length + r * (2.0f + 4.0f * rng.unit());
            }
        }
        capped = int(spots.size()) >= kMaxDabs / 120;
        want = int(spots.size());
    } else {
        // Scattered evenly (a Poisson-disc fill: no two closer than minDist): pores and skin as a field of single
        // dabs (density: 2.6 to 1.4 radii apart), scales over a whole Part 1.5 radii apart; strokes a few radii apart.
        const float lineLength = (o.kind == "scars" ? 14.0f : 12.0f) * r;
        const float minDist = lines ? (o.kind == "scars" ? 6.0f * r : 4.0f * r)
                                    : o.kind == "scales" ? 1.5f * r : r * (2.6f - 1.2f * density);
        const double footprint = lines ? double(lineLength) * double(4.0f * r) : 0.9 * double(minDist) * double(minDist);
        want = int(std::ceil((lines ? density : 1.0) * area / footprint));
        if (o.kind == "scars") want = std::clamp(int(std::lround(density * 5.0)), 1, 8);
        const int most = lines ? kMaxDabs / 100 : kMaxDabs;
        if (want > most) want = most, capped = true;
        const bool smoothNormals = mesh.normals.size() == mesh.positions.size();
        std::unordered_map<int64_t, std::vector<uint32_t>> grid; // spots by cell (minDist), for the spacing
        auto key = [](const glm::ivec3& c) {
            return (int64_t(c.x & 0x1fffff) << 42) | (int64_t(c.y & 0x1fffff) << 21) | int64_t(c.z & 0x1fffff);
        };
        for (int attempt = 0; attempt < want * 8 && int(spots.size()) < want; ++attempt) {
            const double pick = double(rng.unit()) * area;
            const size_t k = size_t(std::lower_bound(cumulative.begin(), cumulative.end(), pick) - cumulative.begin());
            const glm::uvec3& t = mesh.triangles[tris[std::min(k, tris.size() - 1)]];
            float u = rng.unit(), v = rng.unit();
            if (u + v > 1.0f) u = 1.0f - u, v = 1.0f - v;
            const float w = 1.0f - u - v;
            const glm::vec3 p = w * mesh.positions[t.x] + u * mesh.positions[t.y] + v * mesh.positions[t.z];
            const glm::ivec3 c = glm::ivec3(glm::floor(p / minDist));
            bool near = false;
            for (int dx = -1; dx <= 1 && !near; ++dx)
                for (int dy = -1; dy <= 1 && !near; ++dy)
                    for (int dz = -1; dz <= 1 && !near; ++dz) {
                        const auto it = grid.find(key(c + glm::ivec3(dx, dy, dz)));
                        if (it == grid.end()) continue;
                        for (const uint32_t s : it->second)
                            if (glm::dot(spots[s].p - p, spots[s].p - p) < minDist * minDist) near = true;
                    }
            if (near) continue;
            glm::vec3 n = smoothNormals ? w * mesh.normals[t.x] + u * mesh.normals[t.y] + v * mesh.normals[t.z]
                                        : glm::cross(mesh.positions[t.y] - mesh.positions[t.x], mesh.positions[t.z] - mesh.positions[t.x]);
            if (glm::length(n) < 1e-12f) continue;
            n = glm::normalize(n);
            grid[key(c)].push_back(uint32_t(spots.size()));
            Spot spot{p, n};
            // Wrinkles follow the run (+-14 degrees), scars go any way; pores come in mixed sizes, skin is mostly
            // pits with a bump now and then.
            spot.t = runOn(n);
            if (o.kind == "wrinkles") spot.t = rotateAbout(spot.t, n, (rng.unit() - 0.5f) * 0.5f);
            else if (o.kind == "scars") spot.t = rotateAbout(spot.t, n, rng.unit() * 6.2831853f);
            if (lines) spot.length = lineLength * (0.7f + 0.6f * rng.unit());
            if (o.kind == "pores") spot.scale = 0.75f + 0.5f * rng.unit();
            if (o.kind == "skin") spot.scale = 0.8f + 0.4f * rng.unit(), spot.flip = rng.unit() < 0.35f;
            spots.push_back(spot);
        }
    }
    if (spots.empty()) {
        error = "No room for " + o.kind + " in the region";
        return -1;
    }

    if (!doc.sculptor.beginStroke(mesh, std::string("AI detail: ") + o.kind, doc.activeLayer)) {
        error = "Active layer weight is 0 - raise it to sculpt into the layer";
        return -1;
    }
    DabParams base;
    base.radius = r;
    base.strength = strength;
    base.invert = preset->invert != o.invert;
    // A scale's lower rim cut in with Crease: an arc a little inside its edge, so it reads as a plate lying over the
    // row below, not as a bump.
    auto scaleRim = [&](const glm::vec3& centre, const Spot& s) {
        const glm::vec3 down = -glm::cross(s.n, s.t); // (rows run along t; the rim faces away from `direction`'s side)
        DabParams c = base;
        c.radius = 0.22f * r * s.scale;
        c.strength = 0.9f;
        c.invert = o.invert;
        c.viewDir = -s.n;
        c.pickNormal = s.n;
        int n = 0;
        glm::vec3 last{0.0f};
        for (int i = 0; i <= 8; ++i) {
            const float a = 3.14159265f * (0.15f + 0.7f * float(i) / 8.0f); // the lower half, short of the sides
            const glm::vec3 q = centre + 0.85f * r * s.scale * (std::cos(a) * s.t + std::sin(a) * down);
            RayHit hit;
            if (!surfaceRaycast(mesh, doc.bvh, q + s.n * (4.0f * r), -s.n, hit)) continue;
            c.center = hit.position;
            c.strokeDelta = n ? hit.position - last : s.t;
            last = hit.position;
            doc.sculptor.dab(mesh, doc.bvh, BrushType::Crease, c, o.symmetry);
            ++n;
        }
        return n;
    };
    int dabs = 0;
    const float lift = 4.0f * r + (o.whole ? 0.0f : 0.5f * o.radius); // (a row's ends sit off a curved surface)
    for (const Spot& s : spots) {
        DabParams p = base;
        p.viewDir = -s.n;
        p.pickNormal = s.n;
        p.radius = r * s.scale;
        p.invert = base.invert != s.flip;
        if (s.length <= 0.0f) {
            p.center = s.p;
            if (o.kind == "scales" && !o.whole) { // (a row's grid point sits on the tangent plane: onto the surface first)
                RayHit hit;
                if (!surfaceRaycast(mesh, doc.bvh, s.p + s.n * lift, -s.n, hit)) continue;
                p.center = hit.position;
            }
            if (placed) placed->push_back(p.center);
            doc.sculptor.dab(mesh, doc.bvh, preset->type, p, o.symmetry);
            ++dabs;
            if (o.kind == "scales") dabs += scaleRim(p.center, s);
            continue;
        }
        // A stroke through the spot along t, each dab found on the surface along the normal, eased in and out
        // (a wrinkle fades at both ends).
        const int steps = std::max(2, int(std::ceil(s.length / (preset->spacing * r))));
        glm::vec3 last{0.0f};
        bool haveLast = false;
        for (int i = 0; i <= steps && dabs < kMaxDabs * 4; ++i) {
            const float f = float(i) / float(steps);
            const glm::vec3 q = s.p + s.t * ((f - 0.5f) * s.length) + glm::cross(s.n, s.t) * (s.bend * std::sin(3.14159265f * f));
            RayHit hit;
            if (!surfaceRaycast(mesh, doc.bvh, q + s.n * lift, -s.n, hit)) continue;
            p.center = hit.position;
            p.pressure = 0.25f + 0.75f * std::sin(3.14159265f * f);
            p.strokeDelta = haveLast ? hit.position - last : s.t * (preset->spacing * r);
            last = hit.position;
            haveLast = true;
            doc.sculptor.dab(mesh, doc.bvh, preset->type, p, o.symmetry);
            if (placed) placed->push_back(p.center);
            ++dabs;
        }
    }
    UndoRecord record = doc.sculptor.endStroke();
    if (!record.empty()) doc.undo.push(std::move(record));
    if (dabs == 0) {
        error = "Nothing landed on the surface";
        return -1;
    }
    message = std::string(preset->name) + ": " + std::to_string(spots.size()) + (lines ? " strokes (" : " stamps (") +
              std::to_string(dabs) + " dabs) of radius " + num(r) + " (" + num(100.0 * r / diag) + "% of the Part) over " +
              (o.whole ? std::string("the whole Part") : "a radius of " + num(o.radius) + " around " + vec(o.center)) +
              "; edges there " + num(edge) + ". One undo step." +
              (capped ? " (Capped at " + std::to_string(want) + ": call it again on smaller regions for more.)" : "") +
              " Look at it with view_closeup.";
    return dabs;
}

bool closeupFraming(const Document& doc, const glm::vec3& point, float span, const glm::vec3& eye, Closeup& out,
                    std::string& error) {
    float best = std::numeric_limits<float>::max();
    int bestPart = -1;
    uint32_t bestVertex = 0;
    for (int i = 0; i < doc.partCount(); ++i) {
        const Part& part = doc.parts[size_t(i)];
        if (!part.visible) continue;
        const Mesh& m = doc.partMesh(i);
        const glm::vec3 local = part.moved() ? glm::vec3(glm::inverse(part.transform) * glm::vec4(point, 1.0f)) : point;
        const float scale = part.moved() ? glm::length(glm::vec3(part.transform[0])) : 1.0f;
        for (uint32_t v = 0; v < uint32_t(m.positions.size()); ++v) {
            const float d = glm::length(m.positions[v] - local) * scale;
            if (d < best) best = d, bestPart = i, bestVertex = v;
        }
    }
    if (bestPart < 0) {
        error = "Nothing visible to look at";
        return false;
    }
    const Part& part = doc.parts[size_t(bestPart)];
    const Mesh& m = doc.partMesh(bestPart);
    const float scale = part.moved() ? glm::length(glm::vec3(part.transform[0])) : 1.0f;
    const glm::vec3 p = m.positions[bestVertex];
    // The surface's facing there: the normals within a tenth of the span, averaged (one vertex's is noisy on detail).
    glm::vec3 n(0.0f);
    const float near = 0.1f * span / scale;
    size_t inView = 0;
    const float half = 0.5f * span / scale;
    if (m.normals.size() == m.positions.size())
        for (uint32_t v = 0; v < uint32_t(m.positions.size()); ++v)
            if (glm::length(m.positions[v] - p) < near) n += m.normals[v];
    for (const glm::uvec3& t : m.triangles)
        if (glm::length(m.positions[t.x] - p) < half) ++inView;
    if (glm::length(n) < 1e-9f) n = glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::mat3 turn = part.moved() ? glm::mat3(part.transform) : glm::mat3(1.0f);
    out.point = part.moved() ? glm::vec3(part.transform * glm::vec4(p, 1.0f)) : p;
    out.eye = glm::length(eye) > 1e-6f ? glm::normalize(eye) : glm::normalize(turn * n);
    out.part = part.name;
    out.triangles = inView;
    out.edge = medianEdgeNear(m, p, half) * scale;
    return true;
}

std::string closeupText(const Closeup& c, float span) {
    std::string s = "Close-up of " + c.part + " at " + vec(c.point) + ", " + num(span) + " across, looking along " +
                    vec(-c.eye) + ": " + std::to_string(c.triangles) + " triangles in view, edges about " + num(c.edge) +
                    ", so the finest detail the mesh can show here is about " + num(2.5f * c.edge) + " (" +
                    num(250.0f * c.edge / std::max(span, 1e-9f)) + "% of the view).";
    if (c.edge * 2.5f > span / 40.0f)
        s += " That is coarse for pores or wrinkles at this scale: subdivide (or ask detail_pass, which says how much).";
    return s;
}

void registerDetailCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;
    std::vector<ParamSpec> params = {
        {"kind", T::String,
         "pores (pressed-in skin pores), skin (fine folds and grain), wrinkles (parallel rows of lines across the "
         "region, along direction), scales (offset rows of raised scales) or scars (a few deep cuts).",
         true},
        {"center", T::String, "\"x,y,z\": the middle of the region, in scene units (leave out for the whole Part).", false},
        {"radius", T::Number, "The region's radius in scene units (default 15% of the Part's size).", false},
        {"density", T::Number, "0-1, how much of the region it covers (default 0.5; scars: 0.2 = one, 1 = five).", false},
        {"size", T::Number,
         "Each pore's, scale's or line's radius as a fraction of the Part's size (default per kind: pores 0.004, "
         "skin 0.004, wrinkles 0.004, scales 0.012, scars 0.005). The mesh needs edges under size / 2.5: it says so when they aren't.",
         false},
        {"strength", T::Number, "0-1 (default per kind, 0.15-0.9).", false},
        {"invert", T::Bool, "The other way: raised pores and wrinkles, carved scales.", false},
        {"direction", T::String,
         "\"x,y,z\": which way wrinkles run and scales point, projected onto the surface (default horizontal: "
         "forehead lines; \"0,1,0\" for vertical lines such as lip wrinkles).",
         false},
        {"seed", T::Number, "Placement seed (default 1): the same seed gives the same detail.", false},
        {"symmetry", T::String, "Mirror across these planes, e.g. \"x\" (default none).", false},
    };
    reg.add({"detail_pass",
             "Fine surface detail over a region in one undoable step: pores, skin texture, wrinkles, scales or scars, "
             "pressed into the surface along its normal (plain brush dabs, evenly scattered, seeded). Use it on the "
             "last stage, once the forms are done and the mesh is dense (a few million triangles on a head): it "
             "refuses on a coarse mesh and says which subdivide would do. Work region by region (forehead, cheeks, "
             "nose, around the eyes) and check each with view_closeup.",
             params,
             [](Document& doc, const Args& a) -> CommandResult {
                 DetailPassOptions o;
                 o.kind = argString(a, "kind");
                 std::transform(o.kind.begin(), o.kind.end(), o.kind.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                 if (o.kind == "pore") o.kind = "pores";
                 if (o.kind == "wrinkle") o.kind = "wrinkles";
                 if (o.kind == "scale") o.kind = "scales";
                 if (o.kind == "scar") o.kind = "scars";
                 const float diag = glm::length(doc.mesh.bounds().extent());
                 const std::string center = argString(a, "center");
                 if (!center.empty()) {
                     if (!parseVec3(center, o.center)) return {false, "Bad center '" + center + "' (expected x,y,z)"};
                     o.whole = false;
                     o.radius = float(argNumber(a, "radius", 0.15 * diag));
                     if (!(o.radius > 0.0f)) return {false, "radius must be more than 0"};
                 }
                 o.density = float(argNumber(a, "density", 0.5));
                 o.size = float(argNumber(a, "size", 0.0));
                 o.strength = float(argNumber(a, "strength", -1.0));
                 o.invert = argBool(a, "invert", false);
                 const std::string direction = argString(a, "direction");
                 if (!direction.empty() && !parseVec3(direction, o.direction))
                     return {false, "Bad direction '" + direction + "' (expected x,y,z)"};
                 o.seed = uint32_t(std::max(argNumber(a, "seed", 1.0), 0.0));
                 std::string err;
                 if (!symmetryArg(a, doc.mesh, o.symmetry, err)) return {false, err};
                 if (const Part& part = doc.activePartInfo(); part.moved()) { // a moved linked copy: into its space
                     const glm::mat4 inv = glm::inverse(part.transform);
                     o.center = glm::vec3(inv * glm::vec4(o.center, 1.0f));
                     o.direction = glm::mat3(inv) * o.direction;
                     if (const float s = glm::length(glm::vec3(part.transform[0])); s > 0.0f) o.radius /= s;
                 }
                 std::string message;
                 if (detailPass(doc, o, message, err) < 0) return {false, err};
                 return {true, message};
             }});
}

} // namespace sf
