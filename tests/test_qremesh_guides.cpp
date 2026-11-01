// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// QRemesh Guides (ZBrush's ZRemesher Guides): a curve drawn round an "eye" on a sphere gets a closed edge loop
// along it, and the edges near the curve line up with its tangent much better than without the guide; drawing
// (snapping, closing, mirroring), Delete Last / Clear, commands, SubTool swaps and project round trips.
#include "TestHarness.h"

#include "core/Commands.h"
#include "core/Primitives.h"
#include "core/Project.h"
#include "core/QRemesh.h"
#include "core/QRemeshGuides.h"
#include "io/ObjIO.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

using namespace sf;

namespace {

constexpr float kPi = 3.14159265f;

// A circle of angular radius `angle` on the unit sphere around direction `c`.
std::vector<glm::vec3> circleOnSphere(glm::vec3 c, float angle, int n, float radius = 1.0f) {
    c = glm::normalize(c);
    const glm::vec3 u = glm::normalize(glm::cross(c, std::abs(c.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    const glm::vec3 v = glm::cross(c, u);
    std::vector<glm::vec3> out;
    for (int i = 0; i <= n; ++i) { // the last point repeats the first: a drawn loop
        const float phi = 2.0f * kPi * float(i % n) / float(n);
        out.push_back(radius * (std::cos(angle) * c + std::sin(angle) * (std::cos(phi) * u + std::sin(phi) * v)));
    }
    return out;
}

// Unique edges of the result as exported (quads once).
std::vector<std::pair<uint32_t, uint32_t>> faceEdges(const Mesh& m) {
    std::set<std::pair<uint32_t, uint32_t>> edges;
    for (uint32_t t = 0; t < m.triangleCount(); ++t) {
        glm::uvec4 f;
        const int n = m.exportFace(t, f);
        for (int k = 0; k < n; ++k) {
            const uint32_t a = f[k], b = f[(k + 1) % n];
            edges.insert({std::min(a, b), std::max(a, b)});
        }
    }
    return {edges.begin(), edges.end()};
}

// Vertices with 7 or more edges (fans, stars): a defect in a quad mesh. With `reach`, only those within that
// (along the unit sphere) of the circle around `c` at `angle`: the ones a guide there could cause.
size_t highValence(const Mesh& m, glm::vec3 c = glm::vec3(0.0f), float angle = 0.0f, float reach = 0.0f) {
    std::map<uint32_t, int> deg;
    for (auto [a, b] : faceEdges(m)) ++deg[a], ++deg[b];
    size_t n = 0;
    for (auto [v, d] : deg) {
        const float off = std::acos(std::clamp(glm::dot(glm::normalize(m.positions[v]), glm::normalize(c)), -1.0f, 1.0f)) - angle;
        n += d >= 7 && (reach <= 0.0f || std::abs(off) < reach);
    }
    return n;
}

// Six quads (on the unit sphere with about `quads` of them): as far as Curves Strength 100 turns the flow.
float sixQuads(int quads) { return 6.0f * std::sqrt(4.0f * kPi / float(quads)); }

struct GuideFit {
    float meanAngle = 90.0f; // degrees, edges near the curve against its tangent (0 = along or across it)
    size_t nearEdges = 0;
    bool closedLoop = false; // a cycle of edges runs all the way round on the curve
    float loopGap = 360.0f;  // the largest gap (degrees round the circle) in that cycle's coverage
};

// How well the edges of `m` near the circle (around `c` at `angle`, on the unit sphere) follow it.
GuideFit measureFit(const Mesh& m, glm::vec3 c, float angle) {
    c = glm::normalize(c);
    const glm::vec3 u = glm::normalize(glm::cross(c, std::abs(c.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
    const glm::vec3 v = glm::cross(c, u);
    const auto edges = faceEdges(m);
    double len = 0.0;
    for (auto [a, b] : edges) len += glm::length(m.positions[a] - m.positions[b]);
    const float edge = float(len / double(edges.size()));
    // Distance of a point from the circle, and the circle's tangent nearest it.
    auto onCircle = [&](glm::vec3 p, glm::vec3& tangent, float& phi) {
        const glm::vec3 d = glm::normalize(p);
        phi = std::atan2(glm::dot(d, v), glm::dot(d, u));
        const glm::vec3 radial = std::cos(phi) * u + std::sin(phi) * v;
        tangent = -std::sin(phi) * u + std::cos(phi) * v;
        const glm::vec3 q = std::cos(angle) * c + std::sin(angle) * radial;
        return glm::length(p - q);
    };
    GuideFit fit;
    double sum = 0.0;
    std::map<uint32_t, std::vector<uint32_t>> loop; // edges on the curve, along it
    for (auto [a, b] : edges) {
        const glm::vec3 pa = m.positions[a], pb = m.positions[b];
        glm::vec3 t;
        float phi;
        if (onCircle(0.5f * (pa + pb), t, phi) > 0.5f * edge) continue;
        const glm::vec3 d = glm::normalize(pb - pa);
        float deg = std::acos(std::clamp(std::abs(glm::dot(d, t)), 0.0f, 1.0f)) * 180.0f / kPi; // 0..90
        deg = std::min(deg, 90.0f - deg); // a cross field: across the curve is as good as along it
        sum += deg;
        ++fit.nearEdges;
        glm::vec3 ta, tb;
        float fa, fb;
        if (deg < 20.0f && std::acos(std::clamp(std::abs(glm::dot(d, t)), 0.0f, 1.0f)) < 0.5f &&
            onCircle(pa, ta, fa) < 0.3f * edge && onCircle(pb, tb, fb) < 0.3f * edge) {
            loop[a].push_back(b);
            loop[b].push_back(a);
        }
    }
    fit.meanAngle = fit.nearEdges ? float(sum / double(fit.nearEdges)) : 90.0f;
    // The biggest connected set of loop edges: a cycle (every vertex twice) going all the way round.
    std::set<uint32_t> seen;
    for (auto& [start, nb] : loop) {
        if (seen.count(start)) continue;
        std::vector<uint32_t> stack{start}, comp;
        seen.insert(start);
        while (!stack.empty()) {
            const uint32_t x = stack.back();
            stack.pop_back();
            comp.push_back(x);
            for (uint32_t y : loop[x])
                if (seen.insert(y).second) stack.push_back(y);
        }
        bool cycle = comp.size() >= 6;
        std::vector<float> phis;
        for (uint32_t x : comp) {
            cycle &= loop[x].size() == 2;
            glm::vec3 t;
            float phi;
            onCircle(m.positions[x], t, phi);
            phis.push_back(phi * 180.0f / kPi);
        }
        std::sort(phis.begin(), phis.end());
        float gap = 360.0f - (phis.back() - phis.front());
        for (size_t i = 1; i < phis.size(); ++i) gap = std::max(gap, phis[i] - phis[i - 1]);
        if (std::getenv("SF_GUIDE_DEBUG")) {
            size_t d1 = 0, d2 = 0, d3 = 0;
            for (uint32_t x : comp) (loop[x].size() == 1 ? d1 : loop[x].size() == 2 ? d2 : d3)++;
            std::printf("    component %zu vertices: degree 1 %zu, 2 %zu, 3+ %zu, gap %.0f\n", comp.size(), d1, d2, d3, gap);
            if (comp.size() > 10)
                for (uint32_t x : comp) {
                    if (loop[x].size() == 2) continue;
                    glm::vec3 t;
                    float phi;
                    const float d = onCircle(m.positions[x], t, phi);
                    std::printf("      v%u deg %zu at %.0f deg, %.2f edges off; all edges:", x, loop[x].size(), phi * 180.0f / kPi, d / edge);
                    for (auto [a, b] : edges) {
                        if (a != x && b != x) continue;
                        const uint32_t y = a == x ? b : a;
                        glm::vec3 ty;
                        float py;
                        const float dy = onCircle(m.positions[y], ty, py);
                        const glm::vec3 dd = glm::normalize(m.positions[y] - m.positions[x]);
                        std::printf(" [v%u %.2f off, %.0f deg to tangent, len %.2f]", y, dy / edge,
                                    std::acos(std::clamp(std::abs(glm::dot(dd, t)), 0.0f, 1.0f)) * 180.0f / kPi,
                                    glm::length(m.positions[y] - m.positions[x]) / edge);
                    }
                    std::printf("\n");
                }
        }
        if (gap < fit.loopGap) fit.loopGap = gap;
        if (cycle && gap < 30.0f) fit.closedLoop = true;
    }
    return fit;
}

// SF_GUIDE_DEBUG=dir: saves `m` as dir/name.obj with the faces just outside each circle that have an edge on it
// in polygroup 1 (a clean band when the loop is clean), for renders with --polygroups --wireframe.
void debugDump(const char* name, const Mesh& m, std::vector<glm::vec3> eyes, float angle) {
    const char* dir = std::getenv("SF_GUIDE_DEBUG");
    if (!dir) return;
    Mesh copy = m;
    copy.ensurePolygroups();
    const float edge = std::sqrt(4.0f * kPi / float(std::max<size_t>(m.quadCount(), 1)));
    for (uint32_t t = 0; t < copy.triangleCount(); ++t) {
        glm::uvec4 f;
        const uint32_t u = copy.quadPartnerAt(t);
        const int n = copy.exportFace(u == Mesh::kNoQuad ? t : std::min(t, u), f);
        copy.polygroups[t] = 0;
        for (const glm::vec3& eye : eyes) {
            const glm::vec3 c = glm::normalize(eye);
            int on = 0;
            glm::vec3 centre(0.0f);
            for (int k = 0; k < n; ++k) {
                const glm::vec3 p = glm::normalize(copy.positions[f[k]]);
                centre += p;
                // distance from the circle along the sphere, roughly
                on += std::abs(std::acos(std::clamp(glm::dot(p, c), -1.0f, 1.0f)) - angle) < 0.2f * edge;
            }
            const bool outside = std::acos(std::clamp(glm::dot(glm::normalize(centre), c), -1.0f, 1.0f)) > angle;
            if (on >= 2 && outside) copy.polygroups[t] = 1;
        }
    }
    std::string err;
    saveObj(std::string(dir) + "/" + name + ".obj", copy, err);
}

QRemeshGuide guideFrom(const std::vector<glm::vec3>& points, bool closed) {
    QRemeshGuide g;
    g.points = points;
    if (closed) g.points.pop_back();
    g.closed = closed;
    g.stroke = 1;
    return g;
}

} // namespace

TEST(qremesh_guide_loop_round_an_eye) {
    // Six eyes of different sizes and places (off every axis, so the plain field has no reason to circle them).
    const Mesh sphere = makeIcosphere(6);
    struct Eye {
        glm::vec3 at;
        float angle; // angular radius, radians
        int target;
    };
    const Eye eyes[] = {{{0.35f, 0.3f, 0.88f}, 0.42f, 3000}, {{-0.5f, 0.2f, 0.7f}, 0.3f, 4000},
                        {{0.1f, -0.6f, 0.6f}, 0.55f, 2000}, {{0.7f, 0.5f, -0.3f}, 0.35f, 5000},
                        {{-0.2f, 0.9f, 0.3f}, 0.6f, 1500}, {{0.4f, -0.3f, -0.85f}, 0.25f, 3500}};
    int loops = 0;
    for (const Eye& e : eyes) {
        QRemeshOptions o;
        o.targetQuads = e.target;
        o.adaptive = 0.0f;
        Mesh plain, guided;
        std::string err;
        CHECK(qremesh(sphere, o, plain, err));
        o.guides = {guideFrom(circleOnSphere(e.at, e.angle, 240), true)};
        QRemeshStats st;
        CHECK(qremesh(sphere, o, guided, err, {}, &st));
        CHECK(st.guides == 1);
        const TopologyReport topo = topologyReport(guided);
        CHECK(topo.manifold() && topo.boundaryEdges == 0 && topo.quadFraction() >= 0.95f);
        CHECK(std::abs(double(st.quads) / e.target - 1.0) < 0.1);
        const GuideFit a = measureFit(plain, e.at, e.angle), b = measureFit(guided, e.at, e.angle);
        std::printf("  eye %.2f rad, %d quads: without guide %.1f deg (%zu edges, loop %d), with %.1f deg (%zu edges, loop %d)\n",
                    double(e.angle), e.target, a.meanAngle, a.nearEdges, int(a.closedLoop), b.meanAngle, b.nearEdges,
                    int(b.closedLoop));
        CHECK(b.meanAngle < 0.5f * a.meanAngle);
        CHECK(b.meanAngle < 8.0f);
        CHECK(!a.closedLoop);
        loops += b.closedLoop;
        // Fans the guide could cause; QRemesh can leave the odd fold anywhere, guide or not.
        CHECK(highValence(guided, e.at, e.angle, sixQuads(e.target)) == 0);
        debugDump(("guide_eye" + std::to_string(&e - eyes)).c_str(), guided, {e.at}, e.angle);
        if (&e == &eyes[0]) {
            debugDump("guide_plain", plain, {e.at}, e.angle);
            debugDump("guide_guided", guided, {e.at}, e.angle);
            // Deterministic.
            Mesh again;
            CHECK(qremesh(sphere, o, again, err));
            CHECK(again.positions == guided.positions && again.triangles == guided.triangles);
        }
    }
    std::printf("  %d of 6 loops closed\n", loops);
    CHECK(loops == 6);
}

TEST(qremesh_guide_open_curve_turns_the_flow) {
    // An open arc (a third of a tilted circle): the edges along it follow it.
    const Mesh sphere = makeIcosphere(6);
    const glm::vec3 axis(0.6f, 0.7f, -0.3f);
    const float angle = 1.2f;
    std::vector<glm::vec3> arc = circleOnSphere(axis, angle, 300);
    arc.resize(100);
    QRemeshOptions o;
    o.targetQuads = 2500;
    o.adaptive = 0.0f;
    Mesh plain, guided;
    std::string err;
    CHECK(qremesh(sphere, o, plain, err));
    o.guides = {guideFrom(arc, false)};
    CHECK(qremesh(sphere, o, guided, err));
    CHECK(topologyReport(guided).manifold());
    // Measure on the arc's part of the circle only: the rest of the circle isn't guided.
    auto fitOnArc = [&](const Mesh& m) {
        Mesh part = m;
        const glm::vec3 c = glm::normalize(axis);
        const glm::vec3 u = glm::normalize(glm::cross(c, glm::vec3(0, 1, 0))), v = glm::cross(c, u);
        for (uint32_t t = 0; t < part.triangleCount(); ++t) {
            const glm::vec3 p = glm::normalize(part.positions[part.triangles[t].x]);
            float phi = std::atan2(glm::dot(p, v), glm::dot(p, u));
            if (phi < 0.0f) phi += 2.0f * kPi;
            if (phi < 0.25f || phi > 2.0f * kPi / 3.0f - 0.25f) part.triangles[t] = {0, 0, 0}; // off the arc
        }
        return measureFit(part, axis, angle);
    };
    const GuideFit a = fitOnArc(plain), b = fitOnArc(guided);
    std::printf("  arc: without %.1f deg (%zu edges), with %.1f deg (%zu edges)\n", a.meanAngle, a.nearEdges, b.meanAngle,
                b.nearEdges);
    CHECK(b.meanAngle < 0.6f * a.meanAngle);
    CHECK(b.meanAngle < 10.0f);
}

TEST(qremesh_guide_with_symmetry_loops_both_sides) {
    // One loop drawn on the +X side; QRemesh with X symmetry puts a loop round both "eyes".
    const Mesh sphere = makeIcosphere(6);
    const glm::vec3 eye(0.45f, 0.25f, 0.86f), mirrored(-0.45f, 0.25f, 0.86f);
    const float angle = 0.36f;
    QRemeshOptions o;
    o.targetQuads = 3000;
    o.adaptive = 0.0f;
    o.symmetryAxis = 0;
    o.guides = {guideFrom(circleOnSphere(eye, angle, 240), true)};
    Mesh out;
    std::string err;
    QRemeshStats st;
    CHECK(qremesh(sphere, o, out, err, {}, &st));
    CHECK(st.symmetryUsed && st.guides == 1);
    const TopologyReport topo = topologyReport(out);
    CHECK(topo.manifold() && topo.boundaryEdges == 0);
    debugDump("guide_symmetric", out, {eye, mirrored}, angle);
    const GuideFit right = measureFit(out, eye, angle), left = measureFit(out, mirrored, angle);
    std::printf("  symmetric: right %.1f deg loop %d, left %.1f deg loop %d\n", right.meanAngle, int(right.closedLoop),
                left.meanAngle, int(left.closedLoop));
    CHECK(right.closedLoop && left.closedLoop);
    CHECK(right.meanAngle < 8.0f && left.meanAngle < 8.0f);
    CHECK(highValence(out, eye, angle, sixQuads(o.targetQuads)) == 0);
}

TEST(qremesh_guide_loop_across_the_symmetry_plane) {
    // A loop round the mouth, across the middle, with X symmetry: only the right half is remeshed, so the loop
    // is cut there and must meet its mirror image on the seam, at both ends.
    const Mesh sphere = makeIcosphere(6);
    struct Mouth {
        float y, angle;
        int target;
        float maxAngle; // mean degrees off the curve's direction, near it
    };
    // The first is about as small as a loop gets (24 quads round, 12 lattice steps): the ring of quads outside it
    // is half as long again, so six new columns start right beside the loop, each with a diagonal edge there
    // (the loop itself is exact). Over 12 field seeds it measured 4.7-10.7 degrees with and without FMA, and
    // 13.2 on MSVC; 3200 quads on the same loop give 3.6-6.4. The others are not borderline.
    const Mouth mouths[] = {{-0.4f, 0.28f, 2100, 16.0f}, {-0.12f, 0.36f, 2700, 10.0f}, {0.0f, 0.44f, 3300, 10.0f},
                            {0.23f, 0.28f, 4200, 10.0f}};
    int loops = 0;
    for (const Mouth& mo : mouths) {
        const glm::vec3 at(0.0f, mo.y, 0.9f);
        QRemeshOptions o;
        o.targetQuads = mo.target;
        o.adaptive = 0.0f;
        o.symmetryAxis = 0;
        o.guides = {guideFrom(circleOnSphere(at, mo.angle, 240), true)};
        Mesh out;
        std::string err;
        QRemeshStats st;
        CHECK(qremesh(sphere, o, out, err, {}, &st));
        CHECK(st.symmetryUsed && st.guides == 1);
        CHECK(topologyReport(out).manifold());
        const GuideFit f = measureFit(out, at, mo.angle);
        std::printf("  mouth %.2f rad, %d quads: %.1f deg, loop %d\n", double(mo.angle), mo.target, f.meanAngle, int(f.closedLoop));
        debugDump(("guide_mouth" + std::to_string(&mo - mouths)).c_str(), out, {at}, mo.angle);
        CHECK(f.meanAngle < mo.maxAngle);
        CHECK(highValence(out, at, mo.angle, sixQuads(mo.target)) == 0);
        loops += f.closedLoop;
    }
    CHECK(loops == 4);
}

TEST(qremesh_guide_drawing_and_commands) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", 5.0}});
    // A loop seen from the front, mirrored across X: two guides, one stroke.
    std::string pts;
    for (int i = 0; i <= 24; ++i) {
        const float a = 2.0f * kPi * float(i) / 24.0f;
        char buf[64];
        std::snprintf(buf, sizeof buf, "%s%.4f,%.4f,2", i ? " " : "", 0.45 + 0.2 * std::cos(a), 0.2 + 0.2 * std::sin(a));
        pts += buf;
    }
    CommandResult r = reg.execute(doc, "qremesh_guide", {{"points", pts}, {"view", std::string("0,0,-1")},
                                                          {"symmetry", std::string("x")}});
    CHECK(r.ok);
    std::printf("  %s\n", r.message.c_str());
    CHECK(doc.qremeshGuides.size() == 2);
    CHECK(doc.qremeshGuides[0].closed && doc.qremeshGuides[1].closed);
    CHECK(doc.qremeshGuides[0].stroke == doc.qremeshGuides[1].stroke);
    for (const glm::vec3& p : doc.qremeshGuides[0].points) CHECK(std::abs(glm::length(p) - 1.0f) < 0.01f); // on the sphere
    CHECK(doc.qremeshGuides[0].points[0].x > 0.0f && doc.qremeshGuides[1].points[0].x < 0.0f);
    CHECK(doc.modified);
    // An open curve, not mirrored; a loop across the middle mirrored onto itself is kept once.
    CHECK(reg.execute(doc, "qremesh_guide", {{"points", std::string("-0.5,-0.4,2 0,-0.5,2 0.5,-0.4,2")},
                                             {"view", std::string("0,0,-1")}}).ok);
    CHECK(doc.qremeshGuides.size() == 3 && !doc.qremeshGuides[2].closed);
    CHECK(reg.execute(doc, "qremesh_guide", {{"points", std::string("-0.3,0.6,2 0,0.65,2 0.3,0.6,2")},
                                             {"view", std::string("0,0,-1")}, {"symmetry", std::string("x")}}).ok);
    CHECK(doc.qremeshGuides.size() == 4);
    CHECK(reg.execute(doc, "qremesh_guides_info").message.find("4 Quad Remesh guides (2 loops)") != std::string::npos);
    // Too short, or off the model.
    CHECK(!reg.execute(doc, "qremesh_guide", {{"points", std::string("0,0,2 0.001,0,2")}, {"view", std::string("0,0,-1")}}).ok);
    CHECK(!reg.execute(doc, "qremesh_guide", {{"points", std::string("5,5,2 6,5,2")}, {"view", std::string("0,0,-1")}}).ok);
    CHECK(!reg.execute(doc, "qremesh_guide", {{"points", std::string("0,0,2 0.5,0,2")}, {"symmetry", std::string("w")}}).ok);
    // Delete Last takes a stroke (with its mirror), Clear the rest.
    CHECK(reg.execute(doc, "qremesh_guide_delete_last").ok);
    CHECK(doc.qremeshGuides.size() == 3);
    CHECK(reg.execute(doc, "qremesh_guide_delete_last").ok);
    CHECK(doc.qremeshGuides.size() == 2);
    CHECK(reg.execute(doc, "qremesh_guides_clear").ok);
    CHECK(doc.qremeshGuides.empty());
    CHECK(!reg.execute(doc, "qremesh_guide_delete_last").ok);
    CHECK(!reg.execute(doc, "qremesh_guides_clear").ok);
}

TEST(qremesh_guide_command_subtools_and_projects) {
    CommandRegistry reg;
    Document doc;
    reg.execute(doc, "new_sphere", {{"subdivisions", 5.0}});
    CHECK(reg.execute(doc, "qremesh_guide", {{"points", std::string("0.2,0,2 0.4,0.2,2 0.6,0,2 0.4,-0.2,2 0.2,0,2")},
                                             {"view", std::string("0,0,-1")}}).ok);
    const std::vector<QRemeshGuide> drawn = doc.qremeshGuides;
    // Per SubTool: a new SubTool has none, going back brings them back.
    CHECK(reg.execute(doc, "subtool_append", {{"type", std::string("cube")}}).ok);
    CHECK(doc.subtoolCount() == 2 && doc.activeSubtool == 1);
    CHECK(doc.qremeshGuides.empty());
    doc.selectSubtool(0);
    CHECK(doc.qremeshGuides.size() == drawn.size() && doc.qremeshGuides[0].points == drawn[0].points);

    // Projects keep them, per SubTool, exactly.
    const std::string path = (std::filesystem::temp_directory_path() / "sf_test_guides.sfproj").string();
    std::string err;
    doc.selectSubtool(1);
    CHECK(saveProject(path, doc, err)); // the guided SubTool inactive while saving
    Document loaded;
    CHECK(loadProject(path, loaded, err));
    std::filesystem::remove(path);
    CHECK(loaded.activeSubtool == 1 && loaded.qremeshGuides.empty());
    loaded.selectSubtool(0);
    CHECK(loaded.qremeshGuides.size() == 1);
    CHECK(loaded.qremeshGuides[0].closed == drawn[0].closed && loaded.qremeshGuides[0].stroke == drawn[0].stroke);
    CHECK(loaded.qremeshGuides[0].points.size() == drawn[0].points.size());
    float worst = 0.0f;
    for (size_t i = 0; i < drawn[0].points.size(); ++i)
        worst = std::max(worst, glm::length(loaded.qremeshGuides[0].points[i] - drawn[0].points[i]));
    CHECK(worst < 1e-5f);
    // Bad JSON entries are skipped.
    Json bad;
    CHECK(Json::parse("[{\"points\":[1,2]},{\"points\":\"x\"},{\"closed\":true,\"points\":[0,0,1,0,1,0,1,0,0]}]", bad, err));
    const auto parsed = qremeshGuidesFromJson(bad);
    CHECK(parsed.size() == 1 && parsed[0].closed && parsed[0].points.size() == 3);

    // The qremesh command follows them, keeps them for the original, and can ignore them.
    doc.selectSubtool(0);
    CommandResult r = reg.execute(doc, "qremesh", {{"target", 1500.0}, {"keep_original", true}});
    CHECK(r.ok);
    std::printf("  %s\n", r.message.c_str());
    CHECK(r.message.find("followed 1 guide") != std::string::npos);
    CHECK(doc.qremeshGuides.size() == 1);
    CHECK(doc.subtools[size_t(doc.activeSubtool + 1)].qremeshGuides.size() == 1);
    r = reg.execute(doc, "qremesh", {{"target", 1500.0}, {"guides", false}});
    CHECK(r.ok && r.message.find("guide") == std::string::npos);
    // Guides left behind by a move are ignored, with a note.
    for (glm::vec3& p : doc.qremeshGuides[0].points) p += glm::vec3(5.0f, 0.0f, 0.0f);
    r = reg.execute(doc, "qremesh", {{"target", 1500.0}});
    CHECK(r.ok && r.message.find("off the surface") != std::string::npos);
}

// SF_GUIDE_SWEEP=1: closed-loop rate, mean angle and fans near the guides over many eyes, sizes and targets,
// with and without symmetry (for tuning); SF_GUIDE_SWEEP=N: N of them, SF_GUIDE_SEED: the field's seed.
TEST(qremesh_guide_sweep) {
    if (!std::getenv("SF_GUIDE_SWEEP")) return;
    const Mesh sphere = makeIcosphere(6);
    int loops = 0, runs = 0;
    size_t fans = 0, fansPlain = 0;
    double angleSum = 0.0;
    uint32_t seed = 7;
    auto rnd = [&] {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1u << 24);
    };
    const int count = std::max(std::atoi(std::getenv("SF_GUIDE_SWEEP")), 32);
    for (int i = 0; i < count; ++i) {
        const bool sym = i % 2;
        glm::vec3 eye = glm::normalize(glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f));
        const float angle = 0.25f + 0.4f * rnd();
        if (sym) eye = glm::normalize(glm::vec3(std::sin(angle) + 0.05f + 0.4f * rnd(), 0.6f * (rnd() - 0.5f), 0.8f));
        QRemeshOptions o;
        o.targetQuads = 1000 + int(4000 * rnd());
        o.adaptive = 0.0f;
        o.symmetryAxis = sym ? 0 : -1;
        if (const char* seed = std::getenv("SF_GUIDE_SEED")) o.seed = uint32_t(std::atoi(seed));
        Mesh plain, out;
        std::string err;
        if (!qremesh(sphere, o, plain, err)) continue;
        o.guides = {guideFrom(circleOnSphere(eye, angle, 240), true)};
        if (!qremesh(sphere, o, out, err)) continue;
        const GuideFit f = measureFit(out, eye, angle);
        const GuideFit g = sym ? measureFit(out, eye * glm::vec3(-1, 1, 1), angle) : f;
        ++runs;
        loops += f.closedLoop && g.closedLoop;
        angleSum += f.meanAngle;
        const float reach = sixQuads(o.targetQuads);
        const size_t hv = highValence(out, eye, angle, reach) + (sym ? highValence(out, eye * glm::vec3(-1, 1, 1), angle, reach) : 0);
        const size_t hp = highValence(plain, eye, angle, reach) + (sym ? highValence(plain, eye * glm::vec3(-1, 1, 1), angle, reach) : 0);
        fans += hv, fansPlain += hp;
        std::printf("  %2d%s: target %d, angle %.2f: %.1f deg, loop %d%d, fans %zu (plain %zu)\n", i, sym ? " sym" : "    ",
                    o.targetQuads, angle, f.meanAngle, int(f.closedLoop), int(g.closedLoop), hv, hp);
    }
    std::printf("  sweep: %d / %d closed loops, mean %.1f deg, %zu fans near the guides (plain %zu)\n", loops, runs,
                angleSum / std::max(runs, 1), fans, fansPlain);
}
