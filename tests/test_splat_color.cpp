// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sculpt to splats keeps a scan's own colour: a capture goes to a sculptable mesh, the nose is reshaped and
// repainted, and the splats that come back are colour-identical (CIEDE2000) to the capture away from the edit,
// carry the mesh's vertex paint on the new surface, and have no seam between the two.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Primitives.h"
#include "core/Sculptable.h"
#include "core/Splats.h"
#include "core/SurfaceQuery.h"
#include "core/Voxel.h"
#include "core/Image.h"
#include "core/Json.h"
#include "io/PlyIO.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

using namespace sf;

namespace {

// --- CIEDE2000 on display (sRGB) colours -----------------------------------------------------------------------

glm::dvec3 toLab(const glm::vec3& srgb) {
    const glm::vec3 l = srgbToLinear(glm::clamp(srgb, glm::vec3(0.0f), glm::vec3(1.0f)));
    const double X = (0.4124564 * l.x + 0.3575761 * l.y + 0.1804375 * l.z) / 0.95047;
    const double Y = 0.2126729 * l.x + 0.7151522 * l.y + 0.0721750 * l.z;
    const double Z = (0.0193339 * l.x + 0.1191920 * l.y + 0.9503041 * l.z) / 1.08883;
    auto f = [](double t) { return t > 216.0 / 24389.0 ? std::cbrt(t) : (24389.0 / 27.0 * t + 16.0) / 116.0; };
    return {116.0 * f(Y) - 16.0, 500.0 * (f(X) - f(Y)), 200.0 * (f(Y) - f(Z))};
}

double deltaE2000(const glm::dvec3& a, const glm::dvec3& b) {
    const double kPi = 3.14159265358979323846;
    auto deg = [&](double r) { return r * 180.0 / kPi; };
    auto rad = [&](double d) { return d * kPi / 180.0; };
    const double C1 = std::hypot(a.y, a.z), C2 = std::hypot(b.y, b.z), Cm = 0.5 * (C1 + C2);
    const double G = 0.5 * (1.0 - std::sqrt(std::pow(Cm, 7) / (std::pow(Cm, 7) + std::pow(25.0, 7))));
    const double a1 = (1.0 + G) * a.y, a2 = (1.0 + G) * b.y;
    const double c1 = std::hypot(a1, a.z), c2 = std::hypot(a2, b.z);
    auto hue = [&](double y, double x) {
        if (x == 0.0 && y == 0.0) return 0.0;
        const double h = deg(std::atan2(y, x));
        return h < 0.0 ? h + 360.0 : h;
    };
    const double h1 = hue(a.z, a1), h2 = hue(b.z, a2);
    const double dL = b.x - a.x, dC = c2 - c1;
    double dh = 0.0;
    if (c1 * c2 != 0.0) {
        dh = h2 - h1;
        if (dh > 180.0) dh -= 360.0;
        else if (dh < -180.0) dh += 360.0;
    }
    const double dH = 2.0 * std::sqrt(c1 * c2) * std::sin(rad(dh / 2.0));
    const double Lm = 0.5 * (a.x + b.x), cm = 0.5 * (c1 + c2);
    double hm = h1 + h2;
    if (c1 * c2 != 0.0) {
        hm = std::abs(h1 - h2) <= 180.0 ? 0.5 * (h1 + h2) : (h1 + h2 < 360.0 ? 0.5 * (h1 + h2 + 360.0) : 0.5 * (h1 + h2 - 360.0));
    }
    const double T = 1.0 - 0.17 * std::cos(rad(hm - 30.0)) + 0.24 * std::cos(rad(2.0 * hm)) + 0.32 * std::cos(rad(3.0 * hm + 6.0)) -
                     0.20 * std::cos(rad(4.0 * hm - 63.0));
    const double dTheta = 30.0 * std::exp(-std::pow((hm - 275.0) / 25.0, 2));
    const double Rc = 2.0 * std::sqrt(std::pow(cm, 7) / (std::pow(cm, 7) + std::pow(25.0, 7)));
    const double Sl = 1.0 + 0.015 * (Lm - 50.0) * (Lm - 50.0) / std::sqrt(20.0 + (Lm - 50.0) * (Lm - 50.0));
    const double Sc = 1.0 + 0.045 * cm, Sh = 1.0 + 0.015 * cm * T;
    const double Rt = -std::sin(rad(2.0 * dTheta)) * Rc;
    return std::sqrt((dL / Sl) * (dL / Sl) + (dC / Sc) * (dC / Sc) + (dH / Sh) * (dH / Sh) + Rt * (dC / Sc) * (dH / Sh));
}

// Splat positions in world space in a hash grid: the nearest one to a point.
class Nearest {
public:
    Nearest(const SplatCloud& c, float cell) : c_(c), cell_(cell) {
        for (size_t i = 0; i < c.size(); ++i) grid_[key(c.worldPosition(i))].push_back(uint32_t(i));
    }
    // Index of the nearest splat within `radius` other than `skip`, or -1.
    long find(const glm::vec3& p, float radius, long skip = -1) const {
        long best = -1;
        float bestD = radius * radius;
        const int r = int(std::ceil(radius / cell_));
        const glm::ivec3 c = cellOf(p);
        for (int z = -r; z <= r; ++z)
            for (int y = -r; y <= r; ++y)
                for (int x = -r; x <= r; ++x) {
                    const auto it = grid_.find(pack(c + glm::ivec3(x, y, z)));
                    if (it == grid_.end()) continue;
                    for (uint32_t i : it->second) {
                        if (long(i) == skip) continue;
                        const glm::vec3 d = c_.worldPosition(i) - p;
                        const float d2 = glm::dot(d, d);
                        if (d2 < bestD) {
                            bestD = d2;
                            best = long(i);
                        }
                    }
                }
        return best;
    }

private:
    glm::ivec3 cellOf(const glm::vec3& p) const { return glm::ivec3(glm::floor(p / cell_)); }
    static int64_t pack(const glm::ivec3& c) {
        return (int64_t(c.x + 100000) * 200003 + int64_t(c.y + 100000)) * 200003 + int64_t(c.z + 100000);
    }
    int64_t key(const glm::vec3& p) const { return pack(cellOf(p)); }
    const SplatCloud& c_;
    float cell_;
    std::unordered_map<int64_t, std::vector<uint32_t>> grid_;
};

double percentile(std::vector<double> v, double q) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(q * double(v.size())))];
}
double mean(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return v.empty() ? 0.0 : s / double(v.size());
}

} // namespace

namespace {

struct RoundTrip {
    double awayMean = 0, awayP99 = 0, paintedMean = 0, seamP99 = 0, seamCaptureP99 = 0, countRatio = 0;
    size_t away = 0, painted = 0, kept = 0, fresh = 0;
    bool ok = false;
};

// Capture -> Make sculptable -> reshape and repaint a ball (the nose) -> Sculpt to splats, measured against the capture.
// `faceAxis`: the direction the nose pushes out of (the most protruding vertex along it). With `dumpDir`, writes the
// capture, the old grey result and the new one as PLY for pictures.
RoundTrip roundTrip(const SplatCloud& capture, int resolution, const glm::vec3& faceAxis, const char* dumpDir = nullptr,
                    float pushCells = 7.0f, float radiusCells = 14.0f, float tipAbove = 0.0f, bool useBaseline = true,
                    const glm::vec3& paint = glm::vec3(0.9f, 0.1f, 0.8f)) {
    RoundTrip res;
    SculptableOptions so;
    so.surface.resolution = resolution;
    so.faceGuides = false;
    SculptableResult made;
    std::string err;
    CHECK(makeSculptableScan(capture, so, made, err));
    Mesh mesh = made.scan;
    const Mesh original = mesh;
    CHECK(mesh.hasColors());
    const float cell = made.surface.cell;

    mesh.recomputeAllNormals();
    // The most protruding vertex along faceAxis, among those above `tipAbove` of the height (a bust's chest sticks out too).
    const AABB box = mesh.bounds();
    const float minY = box.min.y + tipAbove * box.extent().y;
    size_t tip = 0;
    float best = -1e30f;
    for (size_t v = 0; v < mesh.vertexCount(); ++v) {
        const float d = glm::dot(mesh.positions[v], faceAxis);
        if (mesh.positions[v].y >= minY && d > best) {
            best = d;
            tip = v;
        }
    }
    const glm::vec3 centre = mesh.positions[tip];
    const glm::vec3 pushDir = glm::normalize(mesh.normals[tip]);
    const float R = radiusCells * cell, push = pushCells * cell;
    const glm::vec3& magenta = paint; // (the repaint colour)
    for (size_t v = 0; v < mesh.vertexCount(); ++v) {
        const float d = glm::distance(mesh.positions[v], centre) / R;
        if (d >= 1.0f) continue;
        const float w = 0.5f + 0.5f * std::cos(d * 3.14159265f);
        mesh.positions[v] += pushDir * (push * w);
        if (d < 0.5f) mesh.colors[v] = srgbToLinear(magenta);
    }
    mesh.recomputeAllNormals();
    mesh.markAllDirty();
    const glm::vec3 editedCentre = centre + pushDir * push;

    MeshToSplatsOptions o;
    o.count = 0;
    o.reference = &capture;
    o.baseline = useBaseline ? &original : nullptr;
    MeshToSplatsStats st;
    const SplatCloud out = meshToSplats(mesh, o, &st);
    CHECK(out.validate().empty());
    res.kept = st.kept;
    res.fresh = st.painted + st.clay;

    // Count stays within 10% of the capture's.
    res.countRatio = double(out.size()) / double(capture.size());

    // 1. Away from the edit the colour is the capture's: for every capture splat on the original surface and more
    // than 1.5 R from the edit, the nearest splat of the result.
    TriangleBVH surface;
    surface.build(original);
    const float spacing = std::sqrt(float(surfaceArea(original) / double(capture.size())));
    const Nearest outIndex(out, std::max(spacing, st.tolerance)); // (cells as big as the farthest search)
    std::vector<double> away;
    size_t considered = 0;
    for (size_t i = 0; i < capture.size(); ++i) {
        const glm::vec3 p = capture.worldPosition(i);
        SurfaceHit hit;
        if (capture.opacities[i] < 0.5f || !surface.closestPoint(p, 4.0f * cell, hit)) continue; // the body: within 4 voxels of the scan
        if (glm::distance(p, centre) < 1.5f * R) continue;
        ++considered;
        const long j = outIndex.find(p, std::max(1.5f * spacing, 2.0f * st.tolerance));
        if (j >= 0) away.push_back(deltaE2000(toLab(capture.color(i)), toLab(out.color(size_t(j)))));
    }
    CHECK(considered > 5000 && away.size() > considered * 98 / 100);
    res.away = away.size();
    res.awayMean = mean(away);
    res.awayP99 = percentile(away, 0.99);

    // 2. The edited region takes the vertex paint: output splats within R/4 of the pushed tip are magenta.
    std::vector<double> painted;
    for (size_t j = 0; j < out.size(); ++j)
        if (glm::distance(out.worldPosition(j), editedCentre) < 0.25f * R)
            painted.push_back(deltaE2000(toLab(magenta), toLab(out.color(j))));
    res.painted = painted.size();
    res.paintedMean = mean(painted);

    // 3. No seam: each splat in the band around the edit (R to 1.4 R from the tip) against its nearest neighbour,
    // in the result and in the capture itself.
    auto bandSeam = [&](const SplatCloud& c, const Nearest& idx) {
        std::vector<double> d;
        for (size_t i = 0; i < c.size(); ++i) {
            const float r = glm::distance(c.worldPosition(i), centre);
            if (r < R || r > 1.4f * R) continue;
            const long j = idx.find(c.worldPosition(i), 1.5f * spacing, long(i));
            if (j >= 0) d.push_back(deltaE2000(toLab(c.color(i)), toLab(c.color(size_t(j)))));
        }
        return d;
    };
    const std::vector<double> seamOut = bandSeam(out, outIndex), seamIn = bandSeam(capture, Nearest(capture, spacing));
    res.seamP99 = percentile(seamOut, 0.99);
    res.seamCaptureP99 = percentile(seamIn, 0.99);
    res.ok = seamOut.size() > 100 && seamIn.size() > 100;

    if (dumpDir) {
        // The old result (one grey colour, baked shading) for the before picture.
        MeshToSplatsOptions old; // what it used to do: the mesh's colour ignored, one clay colour with baked shading
        old.count = capture.size();
        Mesh bare = mesh;
        bare.clearColors();
        const SplatCloud grey = meshToSplats(bare, old);
        Mesh none;
        saveSplatPly(std::string(dumpDir) + "/after.ply", out, err, false);
        saveSplatPly(std::string(dumpDir) + "/before-grey.ply", grey, err, false);
        std::ofstream(std::string(dumpDir) + "/edit.txt")
            << centre.x << " " << centre.y << " " << centre.z << "\n" << editedCentre.x << " " << editedCentre.y << " " << editedCentre.z << "\n" << R << "\n";
    }
    char line[900];
    std::snprintf(line, sizeof line, "%s away: n=%zu mean dE2000=%.3f p99=%.3f | painted n=%zu mean dE to vertex paint=%.2f | "
                  "band seam p99=%.2f (capture itself %.2f) | kept %zu of %zu, new %zu | splats %zu -> %zu (x%.3f) | tolerance %.4f\n",
                  useBaseline ? "with scan" : "no scan  ", res.away, res.awayMean, res.awayP99, res.painted, res.paintedMean, res.seamP99, res.seamCaptureP99,
                  st.kept, capture.size(), res.fresh, capture.size(), out.size(), res.countRatio, st.tolerance);
    std::printf("SPLAT_COLOR %s", line);
    if (const char* logDir = std::getenv("SF_SPLAT_COLOR_CC0")) { // (the PC run: numbers kept for the report even when green)
        std::filesystem::create_directories(std::filesystem::path(logDir) / "out");
        std::ofstream(std::filesystem::path(logDir) / "out" / "metrics.log", std::ios::app) << (dumpDir ? "[bust] " : "[head] ") << line;
    }
    return res;
}

} // namespace

TEST(splat_color_roundtrip_edit) {
    // A coloured head as a capture, then Make sculptable: the mesh carries the capture's colour as vertex paint.
    const SplatCloud capture = makeTestCapture(makeTestHead(5), TestCaptureOptions{120000, 3, 1000, false, 5});
    const RoundTrip r = roundTrip(capture, 112, glm::vec3(0, 0, 1));
    // Without the scan to compare against (it was deleted): the splats on the sculpt's own surface stay.
    const RoundTrip plain = roundTrip(capture, 112, glm::vec3(0, 0, 1), nullptr, 7.0f, 14.0f, 0.0f, false);
    CHECK(plain.ok && plain.awayMean < 1.0 && plain.paintedMean < 3.0 && plain.countRatio > 0.9 && plain.countRatio < 1.1);
    CHECK(r.ok);
    CHECK(r.kept > capture.size() / 2);
    CHECK(r.countRatio > 0.9 && r.countRatio < 1.1);
    CHECK(r.awayMean < 1.0 && r.awayP99 < 3.0);
    CHECK(r.painted > 30 && r.paintedMean < 3.0);
    CHECK(r.seamP99 < std::max(6.0, r.seamCaptureP99 + 4.0));
}

TEST(splat_color_sh_and_placement) {
    // Degree-3 colour and the capture's placement survive on the splats that stay; new splats are band 0 only.
    Mesh sphere = makeIcosphere(4);
    MeshToSplatsOptions co;
    co.count = 30000;
    const SplatCloud flat = meshToSplats(sphere, co);
    SplatCloud ref;
    ref.resize(flat.size(), 3);
    for (size_t i = 0; i < flat.size(); ++i) {
        ref.positions[i] = flat.positions[i];
        ref.rotations[i] = flat.rotations[i];
        ref.scales[i] = flat.scales[i];
        ref.opacities[i] = flat.opacities[i];
        for (int k = 0; k < ref.shCount(); ++k)
            ref.sh[i * size_t(ref.shCount()) + size_t(k)] = glm::vec3(0.01f * float(k + 1), 0.02f * float((i + size_t(k)) % 7), -0.03f * float(k));
        ref.setColor(i, glm::vec3(0.2f + 0.6f * std::abs(ref.positions[i].x), 0.3f, 0.7f));
    }
    ref.transform = glm::translate(glm::mat4(1.0f), glm::vec3(2, 0, 0)) * glm::rotate(glm::mat4(1.0f), 1.5707963f, glm::vec3(0, 1, 0)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(1.5f));

    // The mesh in the same world space, its top pushed out (new surface) and painted.
    Mesh world = sphere;
    std::vector<glm::vec3>& col = world.ensureColors();
    for (size_t v = 0; v < world.vertexCount(); ++v) {
        glm::vec3 p = sphere.positions[v];
        if (p.y > 0.8f) {
            p *= 1.35f;
            col[v] = srgbToLinear(glm::vec3(0.9f, 0.8f, 0.1f));
        } else {
            col[v] = srgbToLinear(glm::vec3(0.5f, 0.5f, 0.5f));
        }
        world.positions[v] = glm::vec3(ref.transform * glm::vec4(p, 1.0f));
    }
    world.recomputeAllNormals();
    world.markAllDirty();

    MeshToSplatsOptions o;
    o.count = 0;
    o.reference = &ref;
    MeshToSplatsStats st;
    const SplatCloud out = meshToSplats(world, o, &st);
    CHECK(out.validate().empty() && out.shDegree == 3);
    CHECK(out.transform == ref.transform);
    CHECK(st.kept > ref.size() * 7 / 10 && st.kept < ref.size());
    CHECK(st.painted > 100 && st.clay == 0);
    // Kept splats are the capture's, record for record and in order (all 16 SH coefficients).
    size_t r = 0;
    bool same = true;
    const size_t k = size_t(ref.shCount());
    for (size_t j = 0; j < st.kept && same; ++j) {
        while (r < ref.size() && ref.positions[r] != out.positions[j]) ++r;
        same = r < ref.size() && ref.rotations[r] == out.rotations[j] && ref.scales[r] == out.scales[j] &&
               ref.opacities[r] == out.opacities[j] && std::equal(ref.sh.begin() + std::ptrdiff_t(r * k), ref.sh.begin() + std::ptrdiff_t((r + 1) * k), out.sh.begin() + std::ptrdiff_t(j * k));
    }
    CHECK(same);
    // New ones: yellow paint on the pushed-out cap (the placement is undone for them), no higher bands.
    double yellowErr = 0;
    size_t yellow = 0;
    bool bandsZero = true;
    for (size_t j = st.kept; j < out.size(); ++j) {
        for (size_t q = 1; q < k; ++q) bandsZero = bandsZero && out.sh[j * k + q] == glm::vec3(0.0f);
        if (out.positions[j].y < 1.0f) continue;
        yellowErr += deltaE2000(toLab(glm::vec3(0.9f, 0.8f, 0.1f)), toLab(out.color(j)));
        ++yellow;
    }
    std::printf("SPLAT_COLOR placement: kept %zu of %zu, new %zu (%zu on the cap, mean dE to its paint %.2f)\n", st.kept, ref.size(),
                out.size() - st.kept, yellow, yellow ? yellowErr / double(yellow) : -1.0);
    CHECK(bandsZero && yellow > 100 && yellowErr / double(yellow) < 4.0);
    // New splats sit on the pushed-out cap, in the capture's own frame.
    size_t onCap = 0;
    for (size_t j = st.kept; j < out.size(); ++j) onCap += out.positions[j].y > 0.5f;
    CHECK(onCap > (out.size() - st.kept) * 8 / 10);

    // Our own PLY opens as a standard 3DGS file: f_dc_*, f_rest_* (45 for degree 3), and loads back.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sf_splat_color_test";
    std::filesystem::create_directories(dir);
    std::string err;
    const std::string path = (dir / "back.ply").string();
    CHECK(saveSplatPly(path, out, err, false));
    std::ifstream f(path, std::ios::binary);
    std::string header(2048, '\0');
    f.read(header.data(), std::streamsize(header.size()));
    CHECK(header.find("property float f_dc_2") != std::string::npos);
    CHECK(header.find("property float f_rest_44") != std::string::npos && header.find("f_rest_45") == std::string::npos);
    Mesh dummy;
    SplatCloud back;
    PlyContent content;
    CHECK(loadPly(path, dummy, back, content, err));
    CHECK(back.size() == out.size() && back.shDegree == 3);
    double worst = 0;
    for (size_t j = 0; j < out.size(); j += 11) worst = std::max(worst, double(glm::length(back.color(j) - out.color(j))));
    CHECK(worst < 1e-4);
    std::filesystem::remove_all(dir);
}

TEST(splat_color_fallbacks) {
    // No capture: the mesh's own paint; no paint: Soft Clay; the command keeps the capture when asked to.
    Mesh m = makeIcosphere(4);
    MeshToSplatsOptions o;
    o.count = 8000;
    MeshToSplatsStats st;
    SplatCloud clay = meshToSplats(m, o, &st);
    CHECK(clay.size() == 8000 && st.clay == 8000 && st.painted == 0 && st.kept == 0);
    CHECK(clay.transform == glm::mat4(1.0f));

    std::vector<glm::vec3>& c = m.ensureColors();
    for (size_t v = 0; v < m.vertexCount(); ++v) c[v] = srgbToLinear(glm::vec3(0.1f, 0.6f, 0.2f));
    const SplatCloud painted = meshToSplats(m, o, &st);
    CHECK(painted.size() == 8000 && st.painted == 8000 && st.clay == 0);
    double worst = 0;
    for (size_t i = 0; i < painted.size(); ++i) worst = std::max(worst, deltaE2000(toLab(glm::vec3(0.1f, 0.6f, 0.2f)), toLab(painted.color(i))));
    CHECK(worst < 0.5); // paint as it is: no baked shading

    CommandRegistry reg;
    Document d;
    d.mesh = makeIcosphere(4);
    CHECK(reg.execute(d, "mesh_to_splats", {{"count", 5000.0}}).ok && d.splats.size() == 5000);
    // With a capture loaded and no count: about as many splats as it has, most of them its own.
    SplatCloud cap = meshToSplats(d.mesh, [] { MeshToSplatsOptions q; q.count = 20000; return q; }());
    d.splats = cap;
    const CommandResult r = reg.execute(d, "mesh_to_splats");
    CHECK(r.ok && d.splats.size() > 18000 && d.splats.size() < 22000);
    CHECK(r.message.find("kept from the capture") != std::string::npos);
    CHECK(reg.execute(d, "mesh_to_splats", {{"from_capture", false}, {"count", 3000.0}}).ok && d.splats.size() == 3000);
}

// Opt-in (SF_SPLAT_COLOR_CC0=<folder>): the same round trip on a real photo-textured scan. The folder holds Poly Haven's
// CC0 "Marble Bust 01" (marble_bust_01_1k.gltf, marble_bust_01.bin, textures/marble_bust_01_diff_1k.jpg); its texture is
// sampled into a capture-style splat cloud (200k surfels), and <folder>/out gets the PLYs and numbers for the pictures.
TEST(splat_color_cc0_capture) {
    const char* dirEnv = std::getenv("SF_SPLAT_COLOR_CC0");
    if (!dirEnv) return;
    const std::filesystem::path dir = dirEnv;
    auto readAll = [](const std::filesystem::path& f) {
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    Json g;
    std::string err;
    const std::string bin = readAll(dir / "marble_bust_01.bin");
    if (!Json::parse(readAll(dir / "marble_bust_01_1k.gltf"), g, err) || bin.empty()) {
        CHECK(false && "SF_SPLAT_COLOR_CC0 folder lacks marble_bust_01_1k.gltf / marble_bust_01.bin");
        return;
    }
    const Json& prim = g["meshes"][size_t(0)]["primitives"][size_t(0)];
    auto accessor = [&](int index, int comps) {
        const Json& a = g["accessors"][size_t(index)];
        const Json& v = g["bufferViews"][size_t(a["bufferView"].asNumber())];
        const size_t count = size_t(a["count"].asNumber());
        const size_t base = size_t(v["byteOffset"].asNumber(0)) + size_t(a["byteOffset"].asNumber(0));
        const int type = int(a["componentType"].asNumber());
        const size_t width = type == 5126 || type == 5125 ? 4 : 2;
        const size_t stride = size_t(v["byteStride"].asNumber(double(width * size_t(comps))));
        std::vector<double> out(count * size_t(comps));
        for (size_t i = 0; i < count; ++i)
            for (int c = 0; c < comps; ++c) {
                const char* at = bin.data() + base + i * stride + size_t(c) * width;
                if (type == 5126) { float f; std::memcpy(&f, at, 4); out[i * size_t(comps) + size_t(c)] = f; }
                else if (type == 5125) { uint32_t u; std::memcpy(&u, at, 4); out[i * size_t(comps) + size_t(c)] = u; }
                else { uint16_t u; std::memcpy(&u, at, 2); out[i * size_t(comps) + size_t(c)] = u; }
            }
        return out;
    };
    const std::vector<double> pos = accessor(int(prim["attributes"]["POSITION"].asNumber()), 3);
    const std::vector<double> uv = accessor(int(prim["attributes"]["TEXCOORD_0"].asNumber()), 2);
    const std::vector<double> idx = accessor(int(prim["indices"].asNumber()), 1);
    Image tex;
    if (!Image::load((dir / "textures" / "marble_bust_01_diff_1k.jpg").string(), tex, err)) {
        CHECK(false && "texture missing");
        return;
    }
    CHECK(pos.size() > 3000 && uv.size() / 2 == pos.size() / 3 && idx.size() % 3 == 0 && !tex.empty());

    // Height 2, centred; the surface sampled with the texture's colour (display sRGB) at each point.
    AABB b;
    for (size_t i = 0; i < pos.size(); i += 3) b.expand(glm::vec3(float(pos[i]), float(pos[i + 1]), float(pos[i + 2])));
    const float k = 2.0f / b.extent().y;
    const glm::vec3 mid = b.center();
    std::vector<glm::vec3> P(pos.size() / 3);
    for (size_t i = 0; i < P.size(); ++i) P[i] = (glm::vec3(float(pos[3 * i]), float(pos[3 * i + 1]), float(pos[3 * i + 2])) - mid) * k;
    auto texel = [&](glm::vec2 t) {
        const float x = std::clamp(t.x - std::floor(t.x), 0.0f, 1.0f) * float(tex.width - 1);
        const float y = std::clamp(t.y - std::floor(t.y), 0.0f, 1.0f) * float(tex.height - 1);
        const int x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, tex.width - 1), y1 = std::min(y0 + 1, tex.height - 1);
        const float fx = x - float(x0), fy = y - float(y0);
        auto at = [&](int px, int py) { const uint8_t* p = &tex.rgba[(size_t(py) * size_t(tex.width) + size_t(px)) * 4]; return glm::vec3(p[0], p[1], p[2]) / 255.0f; };
        return glm::mix(glm::mix(at(x0, y0), at(x1, y0), fx), glm::mix(at(x0, y1), at(x1, y1), fx), fy);
    };
    std::vector<double> cumulative;
    double total = 0;
    for (size_t t = 0; t < idx.size(); t += 3) {
        const glm::vec3 &a = P[size_t(idx[t])], &bb = P[size_t(idx[t + 1])], &c = P[size_t(idx[t + 2])];
        total += 0.5 * double(glm::length(glm::cross(bb - a, c - a)));
        cumulative.push_back(total);
    }
    const size_t count = 200000;
    const float spacing = float(std::sqrt(total / double(count)));
    SplatCloud capture;
    capture.name = "Marble Bust";
    capture.resize(count, 0);
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> pick(0.0, total);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (size_t i = 0; i < count; ++i) {
        const size_t tri = std::min(size_t(std::upper_bound(cumulative.begin(), cumulative.end(), pick(rng)) - cumulative.begin()), cumulative.size() - 1);
        const size_t i0 = size_t(idx[3 * tri]), i1 = size_t(idx[3 * tri + 1]), i2 = size_t(idx[3 * tri + 2]);
        float u = unit(rng), v = unit(rng);
        if (u + v > 1.0f) { u = 1.0f - u; v = 1.0f - v; }
        const float w = 1.0f - u - v;
        const glm::vec3 n = glm::normalize(glm::cross(P[i1] - P[i0], P[i2] - P[i0]) + glm::vec3(1e-12f));
        auto uvOf = [&](size_t i) { return glm::vec2{float(uv[2 * i]), float(uv[2 * i + 1])}; };
        const glm::vec2 t0 = uvOf(i0), t1 = uvOf(i1), t2 = uvOf(i2);
        const glm::vec3 d = glm::cross(glm::vec3(0, 0, 1), n);
        capture.positions[i] = w * P[i0] + u * P[i1] + v * P[i2];
        capture.rotations[i] = glm::dot(glm::vec3(0, 0, 1), n) < -0.999999f ? glm::quat(0, 1, 0, 0)
                                                                              : glm::normalize(glm::quat(1.0f + glm::dot(glm::vec3(0, 0, 1), n), d.x, d.y, d.z));
        capture.scales[i] = glm::vec3(spacing * 0.6f, spacing * 0.6f, spacing * 0.08f);
        capture.opacities[i] = 0.95f;
        capture.setColor(i, texel(w * t0 + u * t1 + v * t2));
    }
    const std::filesystem::path out = dir / "out";
    std::filesystem::create_directories(out);
    CHECK(saveSplatPly((out / "capture.ply").string(), capture, err, false));

    glm::vec3 face(0, 0, 1);
    if (const char* f = std::getenv("SF_SPLAT_COLOR_FACE")) std::sscanf(f, "%f,%f,%f", &face.x, &face.y, &face.z);
    const std::string outDir = out.string();
    const RoundTrip r = roundTrip(capture, 192, face, outDir.c_str(), 7.0f, 14.0f, 0.55f, true, glm::vec3(0.80f, 0.42f, 0.28f));
    CHECK(r.ok && r.countRatio > 0.9 && r.countRatio < 1.1);
    CHECK(r.awayMean < 1.0 && r.awayP99 < 3.0);
    CHECK(r.paintedMean < 3.0);
    std::ofstream(out / "metrics.txt") << "away mean dE2000 " << r.awayMean << " p99 " << r.awayP99 << " (n=" << r.away << ")\n"
                                       << "painted mean dE " << r.paintedMean << " (n=" << r.painted << ")\n"
                                       << "band seam p99 " << r.seamP99 << " capture itself " << r.seamCaptureP99 << "\n"
                                       << "kept " << r.kept << " new " << r.fresh << " of " << capture.size() << " ratio " << r.countRatio << "\n";
}

TEST(splat_color_after_make_sculptable) {
    // The real pipeline: Make sculptable (a coarse quad mesh, 2500 faces) and straight back to splats with nothing
    // sculpted: the capture comes back, not a grey copy of the coarse mesh.
    Document d;
    CommandRegistry reg;
    CHECK(reg.execute(d, "splat_test_capture", {{"kind", std::string("head")}, {"count", 120000.0}}).ok);
    const size_t captured = d.splats.size();
    const SplatCloud before = d.splats;
    CHECK(reg.execute(d, "splat_to_mesh", {{"detail", 128.0}, {"target", 2500.0}}).ok);
    const CommandResult r = reg.execute(d, "mesh_to_splats");
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.ok && r.message.find("kept from the capture") != std::string::npos);
    CHECK(d.splats.name == before.name); // (Send to Blender updates the same object)
    const double ratio = double(d.splats.size()) / double(captured);
    CHECK(ratio > 0.9 && ratio < 1.1);
    // Nearly all of the output is the capture's own splats (the same positions), and its colour is untouched.
    std::unordered_map<uint64_t, size_t> at;
    auto key = [](const glm::vec3& p) {
        uint64_t k = 1469598103934665603ull;
        for (int c = 0; c < 3; ++c) { uint32_t u; std::memcpy(&u, &p[c], 4); k = (k ^ u) * 1099511628211ull; }
        return k;
    };
    for (size_t i = 0; i < before.size(); ++i) at[key(before.positions[i])] = i;
    size_t same = 0;
    for (size_t j = 0; j < d.splats.size(); ++j) {
        const auto it = at.find(key(d.splats.positions[j]));
        if (it != at.end() && before.sh[it->second] == d.splats.sh[j]) ++same;
    }
    std::printf("  %zu of %zu splats are the capture's own\n", same, d.splats.size());
    CHECK(same > d.splats.size() * 85 / 100);
}
