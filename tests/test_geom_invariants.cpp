// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every brush and tool keeps its promise on a dense curved mesh (tests/GeomInvariants.h, docs/testing-flow.md
// "Brush and tool promises"). One <name>_invariant test per library brush (registered from builtinBrushRecipes(),
// so a new brush gets one by itself) and per built-in brush type, then the tools: remesh, decimate, booleans, Cut,
// dynamic topology, Smooth and Mask.
//
// A brush's promise is read from its recipe, as its description and the guide state it:
//   cut-only   (Scrape planes, Crisp Flatten, carving tips): no vertex rises along its normal
//   build-only (Clay and Fill planes, raising tips):         no vertex sinks
//   every brush: nothing NaN, no flipped triangles, nothing moves outside the brush, the X mirror holds, and with a
//   round untextured tip no spike in the falloff ring and no climbing step (ring, terrace) in a dab's profile.
// Two overlapping pen strokes (pressure 0.2 -> 1 and 1 -> 0.2) carry all of it.
//
// A red found on main is a filed bug: kKnown lists it with its issue, the test prints KNOWN and stays green, and
// turns red once it passes (the fix's branch then removes its line, so a fix can't go unnoticed or regress quietly).
#include "GeomInvariants.h"

#include "core/BrushAlpha.h"
#include "core/BrushRecipe.h"
#include "core/Cut.h"
#include "core/Decimate.h"
#include "core/QRemesh.h"
#include "core/Voxel.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <string>

using namespace sf;
using namespace sf::geom;

namespace {

constexpr Symmetry kNoSymmetry{false, false, false};
constexpr Symmetry kMirrorX{true, false, false};

// Reds on main, filed (test name -> issue). Remove a line in the branch that fixes it.
const std::map<std::string, const char*> kKnown = {
    {"crisp_flatten_invariant", "fix/crisp-flatten-cut (rises, edge spikes)"},
};

// A check that says what an artist would see when it fails.
void expect(bool ok, const char* fmt, ...) {
    ++::sf::test::g_checks;
    if (ok) return;
    ++::sf::test::g_failures;
    std::printf("  FAILED: ");
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    va_end(a);
    std::printf("\n");
}

// Runs `body`; a test in kKnown reports its failures as KNOWN (green) and fails once it passes.
void guarded(const std::string& name, const std::function<void()>& body) {
    const int before = ::sf::test::g_failures;
    body();
    auto it = kKnown.find(name);
    if (it == kKnown.end()) return;
    if (::sf::test::g_failures > before) {
        std::printf("  KNOWN %s: %d check(s) red, as filed\n", it->second, ::sf::test::g_failures - before);
        ::sf::test::g_failures = before;
    } else {
        expect(false, "%s passes now: remove its kKnown line (%s) in the fixing branch", name.c_str(), it->second);
    }
}

std::string slug(const std::string& name) {
    std::string s;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) s += char(std::tolower(static_cast<unsigned char>(c)));
        else if (!s.empty() && s.back() != '_') s += '_';
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s;
}

const std::vector<uint32_t>& sphereMirror() {
    static const std::vector<uint32_t> map = mirrorMapX(denseSphereFixture().mesh);
    return map;
}

// --- the promise of a brush ---

enum class Dir { Any, CutOnly, BuildOnly };
struct Promise {
    Dir dir = Dir::Any;
    bool cleanEdge = false; // untextured: no spike in the falloff ring
    bool profile = false;   // round, untextured, along the normal: a dab's profile never climbs outward
    float reach = 1.1f;     // radii a dab may move anything
    float slack = 0.0f;     // direction tolerance, as a fraction of the largest move (Smooth's own settle)
    bool layer = false;     // a stroke crossing itself never stacks past its own height
    bool folds = true;      // no triangle is folded inside out (not Tendril: see drag_brushes_replay_session)
    bool flips = true;      // no triangle turns past 90 degrees (not for brushes that pull or bend the surface far)
    bool rough = false;     // needs relief to act on (Smooth): runs on the sphere with symmetric noise laid on
    float spike = kSpike;   // the edge spike threshold, in mean edges
    float spacing = 0.125f; // dab spacing, a fraction of the brush size
};
const char* dirName(Dir d) { return d == Dir::CutOnly ? "cut-only" : d == Dir::BuildOnly ? "build-only" : "any"; }

Promise promiseOf(const BrushRecipe& r) {
    using P = BrushRecipe::Plane;
    Promise p;
    const bool patterned = r.pattern != BrushRecipe::Pattern::None && r.patternAmount > 0.0f;
    const bool alpha = !r.alpha.empty() && r.alpha != "Round";
    const bool alongNormal = r.direction == BrushRecipe::Direction::Normal || r.direction == BrushRecipe::Direction::Inflate;
    if (r.drag <= 0.0f && !patterned && alongNormal) {
        if (r.plane == P::Scrape || (r.plane == P::Flatten && r.crispEdge)) p.dir = Dir::CutOnly;
        else if (r.plane == P::Fill || (r.plane == P::Clay && r.depth >= 0.0f)) p.dir = Dir::BuildOnly;
        else if (r.plane == P::None && r.depth > 0.0f) p.dir = Dir::BuildOnly;
        else if (r.plane == P::None && r.depth < 0.0f) p.dir = Dir::CutOnly;
    }
    p.cleanEdge = !patterned && !alpha;
    p.profile = p.cleanEdge && r.plane == P::None && r.pinch == 0.0f && r.drag == 0.0f && alongNormal && !r.crispEdge;
    p.reach = (alpha ? 1.45f : 1.1f) * (1.0f + r.jitter) + r.drag;
    p.slack = r.smooth > 0.0f ? 0.05f : 0.0f;
    p.flips = r.drag < 0.5f;
    if (r.drag >= 0.5f) p.cleanEdge = false; // (a smear: the edge of what it drags along is the brush's own)
    p.spacing = std::max(r.spacing, 0.125f);
    return p;
}

Promise promiseOf(BrushType t) {
    Promise p;
    p.cleanEdge = true;
    switch (t) {
    case BrushType::Standard: case BrushType::Inflate: p.dir = Dir::BuildOnly, p.profile = true; break;
    case BrushType::Clay: p.dir = Dir::BuildOnly; break;
    case BrushType::ClayBlock: // (square-ish: it reaches into its corners, which are a crease by design)
        p.dir = Dir::BuildOnly, p.reach = 1.45f, p.spike = 0.35f; break;
    case BrushType::Layer: p.dir = Dir::BuildOnly, p.layer = true; break;
    case BrushType::Trim: p.dir = Dir::CutOnly; break;
    case BrushType::SmoothPeaks: p.dir = Dir::CutOnly, p.rough = true, p.cleanEdge = false; break;
    case BrushType::Smooth: p.rough = true, p.cleanEdge = false; break;
    case BrushType::Crease: p.dir = Dir::CutOnly; break;
    case BrushType::Vdm: p.cleanEdge = false, p.flips = false, p.reach = 1.45f, p.spacing = 1.0f; break; // (stamps, overhangs)
    case BrushType::SnakeHook: // (pulls the region along the stroke: on arcs laid on the unmoved surface a crossing folds it, which no
                               // artist stroke does: recorded strokes judge that, drag_brushes_replay_session)
        p.reach = 2.5f, p.flips = false, p.cleanEdge = false, p.folds = false; break;
    case BrushType::Nudge: case BrushType::KelvinSnake: case BrushType::Cloth:
        p.reach = 2.5f, p.flips = false, p.cleanEdge = false; break; // (they pull the region along the stroke)
    case BrushType::KelvinTwist: case BrushType::KelvinScale: p.reach = 3.0f; break; // (elastic: fades, never cut off)
    default: break;
    }
    return p;
}

// The dense sphere with symmetric relief (0.4 edges of hashed noise along the normals near `at` and its mirror
// image), for brushes that only act on relief.
Document roughSphere(glm::vec3 at) {
    Document d = denseSphere();
    const float edge = meanEdge(d.mesh);
    for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
        glm::vec3 p = d.mesh.positions[v];
        const glm::vec3 q(std::abs(p.x), p.y, p.z);
        if (glm::distance(q, glm::vec3(std::abs(at.x), at.y, at.z)) > 0.35f) continue;
        const uint32_t h = ::sf::test::shardHash(std::to_string(std::lround(q.x * 1e5)).append(":")
                                                     .append(std::to_string(std::lround(q.y * 1e5)))
                                                     .append(":").append(std::to_string(std::lround(q.z * 1e5))).c_str());
        d.mesh.positions[v] = p + d.mesh.normals[v] * (0.4f * edge * (float(h % 2001) / 1000.0f - 1.0f));
    }
    d.mesh.recomputeAllNormals();
    d.bvh.build(d.mesh);
    return d;
}

using Dabber = std::function<void(Document&, const DabParams&, Symmetry)>;

void strokes(Document& d, const char* label, const Dabber& dab, const std::vector<std::vector<DabParams>>& all, Symmetry sym) {
    for (const auto& s : all) {
        d.sculptor.beginStroke(d.mesh, label);
        for (const DabParams& p : s) dab(d, p, sym);
        d.undo.push(d.sculptor.endStroke());
    }
}

// The whole check of one brush on the dense sphere (and, for plane brushes, the saddle).
void checkBrush(const std::string& name, const Promise& pr, const Dabber& dab, bool plane, float strength = 0.5f) {
    constexpr float kRadius = 0.15f;
    const glm::vec3 at = glm::normalize(glm::vec3(0.45f, 0.6f, 0.66f)); // (off the mirror plane)
    auto all = overlapStrokes(at, kRadius, strength, pr.spacing);
    const std::vector<glm::vec3> centers = centersOf(all);
    std::vector<glm::vec3> mirrored = centers;
    for (glm::vec3& c : mirrored) c.x = -c.x;
    std::vector<glm::vec3> both = centers;
    both.insert(both.end(), mirrored.begin(), mirrored.end());

    Document d = pr.rough ? roughSphere(at) : denseSphere();
    const std::vector<glm::vec3> before = d.mesh.positions, normals = d.mesh.normals;
    const float edge = meanEdge(denseSphereFixture().mesh);
    strokes(d, name.c_str(), dab, all, kMirrorX);

    const float moved = maxMove(before, d.mesh);
    const float rise = maxRise(before, d.mesh, normals), sink = maxSink(before, d.mesh, normals);
    const float outside = maxMoveOutside(before, d.mesh, both, pr.reach * kRadius);
    const float sym = symmetryError(d.mesh, sphereMirror());
    const size_t nan = nonFinite(d.mesh), flips = flippedFaces(before, d.mesh), inverted = invertedFaces(before, d.mesh);
    const EdgeReport e = edgeOutliers(d.mesh, before, both, kRadius, edge);
    std::printf("  %s [%s]: moved %.4f (edge %.4f), rise %.2g sink %.2g, outside %.2g, mirror %.2g, flips %zu, "
                "inside out %zu, edge umbrella %.3f (interior %.3f, %d ring verts, %d spikes)\n",
                name.c_str(), dirName(pr.dir), moved, edge, rise, sink, outside, sym, flips, inverted, e.worst, e.interior,
                e.ring, e.spikes);
    expect(moved > 0.05f * edge, "%s: two strokes moved nothing (largest move %.3g)", name.c_str(), moved);
    expect(nan == 0, "%s: %zu vertices became NaN or infinite", name.c_str(), nan);
    if (pr.folds) expect(inverted == 0, "%s: %zu triangles folded inside out (dark specks)", name.c_str(), inverted);
    if (pr.flips) expect(flips == 0, "%s: %zu triangles flipped over", name.c_str(), flips);
    expect(outside < 1e-5f, "%s: a vertex %.3g away from the brush (past %.2f radii) moved %.3g", name.c_str(),
           pr.reach * kRadius, pr.reach, outside);
    expect(sym < 1e-4f, "%s: X symmetry drifted by %.3g (%.2f edges): the two sides differ", name.c_str(), sym, sym / edge);
    const float tol = 1e-5f + pr.slack * moved;
    if (pr.dir == Dir::CutOnly)
        expect(rise <= tol, "%s promises to only cut, but a vertex rose %.3g (%.2f edges)", name.c_str(), rise, rise / edge);
    if (pr.dir == Dir::BuildOnly)
        expect(sink <= tol, "%s promises to only build up, but a vertex sank %.3g (%.2f edges)", name.c_str(), sink,
               sink / edge);
    if (pr.cleanEdge)
        expect(e.worst <= pr.spike, "%s: %d spikes or stair-steps along the falloff edge (worst %.2f edges out of line)",
               name.c_str(), e.spikes, e.worst);

    if (pr.layer) { // a stroke crossing itself never stacks: no higher than either of its halves alone
        Document a = denseSphere(), b = denseSphere(), ab = denseSphere();
        strokes(a, name.c_str(), dab, {all[0]}, kNoSymmetry);
        strokes(b, name.c_str(), dab, {all[1]}, kNoSymmetry);
        std::vector<DabParams> crossing = all[0];
        crossing.insert(crossing.end(), all[1].begin(), all[1].end());
        strokes(ab, name.c_str(), dab, {crossing}, kNoSymmetry);
        const float h1 = std::max(maxRise(before, a.mesh, normals), maxRise(before, b.mesh, normals));
        const float h2 = maxRise(before, ab.mesh, normals);
        std::printf("  %s crossing itself: %.4g (halves alone %.4g)\n", name.c_str(), h2, h1);
        expect(h2 <= 1.05f * h1 + 1e-5f, "%s: a stroke crossing itself stacked to %.3g, past %.3g", name.c_str(), h2, h1);
    }

    if (pr.profile) { // one dab: the falloff only comes down going outward
        Document one = denseSphere();
        DabParams p = all[0][0];
        p.center = at, p.viewDir = -at, p.pressure = 1.0f, p.strokeDelta = glm::vec3(0.0f);
        strokes(one, name.c_str(), dab, {{p}}, kNoSymmetry);
        const Profile prof = radialProfile(one.mesh, before, normals, at, kRadius, edge);
        std::printf("  %s one dab: %d climbing steps (worst %.1f%% of the peak)\n", name.c_str(), prof.steps,
                    100.0f * prof.worstClimb);
        expect(prof.steps == 0, "%s: a dab's falloff climbs again toward its edge (a ring or terrace, %.1f%% of its height)",
               name.c_str(), 100.0f * prof.worstClimb);
    }

    if (plane) { // the saddle: surface above and below the plane in every dab
        Document s = docFrom(saddleFixture());
        const std::vector<glm::vec3> sb = s.mesh.positions, sn = s.mesh.normals;
        const float se = meanEdge(s.mesh);
        std::vector<DabParams> line;
        const float sstep = std::max(0.05f, 0.4f * pr.spacing);
        const int scount = std::clamp(1 + int(1.2f / sstep), 2, 25);
        for (int i = 0; i < scount; ++i) {
            DabParams p;
            const float x = -0.6f + sstep * float(i), z = 0.15f;
            p.center = glm::vec3(x, 0.35f * (x * x - z * z), z);
            p.viewDir = glm::vec3(0, -1, 0);
            p.radius = 0.2f, p.strength = strength, p.pressure = 0.2f + 0.8f * float(i) / float(scount - 1);
            p.strokeDelta = i ? glm::vec3(sstep, 0.0f, 0.0f) : glm::vec3(0.0f);
            line.push_back(p);
        }
        strokes(s, name.c_str(), dab, {line}, kNoSymmetry);
        std::vector<glm::vec3> lc;
        for (const DabParams& p : line) lc.push_back(p.center);
        const float r2 = maxRise(sb, s.mesh, sn), s2 = maxSink(sb, s.mesh, sn), m2 = maxMove(sb, s.mesh);
        const EdgeReport e2 = edgeOutliers(s.mesh, sb, lc, 0.2f, se);
        std::printf("  %s saddle: moved %.4f, rise %.2g sink %.2g, edge umbrella %.3f (%d spikes), flips %zu\n",
                    name.c_str(), m2, r2, s2, e2.worst, e2.spikes, flippedFaces(sb, s.mesh));
        const float t2 = 1e-5f + pr.slack * m2;
        if (pr.dir == Dir::CutOnly)
            expect(r2 <= t2, "%s on a saddle: a vertex rose %.3g (%.2f edges)", name.c_str(), r2, r2 / se);
        if (pr.dir == Dir::BuildOnly)
            expect(s2 <= t2, "%s on a saddle: a vertex sank %.3g (%.2f edges)", name.c_str(), s2, s2 / se);
        if (pr.cleanEdge)
            expect(e2.worst <= pr.spike, "%s on a saddle: %d spikes along the falloff edge (worst %.2f edges)", name.c_str(),
                   e2.spikes, e2.worst);
        expect(nonFinite(s.mesh) == 0 && invertedFaces(sb, s.mesh) == 0 && (!pr.flips || flippedFaces(sb, s.mesh) == 0),
               "%s on a saddle: NaN, inside-out or flipped triangles (%zu flipped)", name.c_str(), flippedFaces(sb, s.mesh));
    }
}

// --- registration: one test per library brush and per built-in brush type ---

std::deque<std::string>& names() {
    static std::deque<std::string> n;
    return n;
}

void addTest(const std::string& name, std::function<void()> fn) {
    names().push_back(name);
    static std::deque<::sf::test::Register> regs;
    regs.emplace_back(names().back().c_str(), [name, fn] { guarded(name, fn); }, __FILE__);
}

[[maybe_unused]] const bool kRegistered = [] {
    for (const BrushRecipe& r : builtinBrushRecipes()) {
        addTest(slug(r.name) + "_invariant", [r] {
            const Promise pr = promiseOf(r);
            auto alpha = std::make_shared<BrushAlpha>();
            const bool hasAlpha = !r.alpha.empty() && BrushAlpha::builtin(r.alpha, *alpha);
            checkBrush(r.name, pr,
                       [r, alpha, hasAlpha](Document& d, const DabParams& p, Symmetry sym) {
                           DabParams q = p;
                           if (hasAlpha) q.alpha = alpha.get();
                           d.sculptor.dabRecipe(d.mesh, d.bvh, r, q, sym);
                       },
                       r.plane != BrushRecipe::Plane::None);
        });
    }
    for (int i = 0; i < int(BrushType::Count); ++i) {
        const BrushType t = BrushType(i);
        // Grab-style brushes (Move, Kelvin Grab) drag a captured region and Morph and Detail Clone need a target:
        // they have their own tests (test_grab_coalesce, test_morph, test_kelvinlet).
        if (t == BrushType::Move || t == BrushType::KelvinGrab || t == BrushType::Morph || t == BrushType::DetailClone)
            continue;
        addTest("brush_" + slug(brushName(t)) + "_invariant", [t] {
            const bool plane = t == BrushType::Clay || t == BrushType::ClayBlock || t == BrushType::Flatten ||
                               t == BrushType::Trim;
            checkBrush(brushName(t), promiseOf(t),
                       [t](Document& d, const DabParams& p, Symmetry sym) {
                           d.sculptor.dab(d.mesh, d.bvh, t, p, sym);
                       },
                       plane);
        });
    }
    return true;
}();

// One dab's radial profile at `radius` on the dense sphere (no symmetry).
Profile oneDab(BrushType t, float radius, float& peak) {
    Document d = denseSphere();
    const std::vector<glm::vec3> before = d.mesh.positions, normals = d.mesh.normals;
    const glm::vec3 at = glm::normalize(glm::vec3(0.45f, 0.6f, 0.66f));
    DabParams p;
    p.center = at, p.viewDir = -at, p.radius = radius, p.strength = 0.5f;
    strokes(d, brushName(t), [t](Document& x, const DabParams& q, Symmetry s) { x.sculptor.dab(x.mesh, x.bvh, t, q, s); },
            {{p}}, kNoSymmetry);
    peak = maxRise(before, d.mesh, normals);
    return radialProfile(d.mesh, before, normals, at, radius, meanEdge(denseSphereFixture().mesh));
}

// The dragging brushes on the strokes of a real close-up session (tests/data/drag_paths, GeomInvariants.h replayStrokes):
// the four fixtures one after the other on one sphere, each step's folds counted against the mesh the step before left
// (what the artist's report lists; X symmetry, a second stroke crossing a dragged area, a long pull). 0.12.7 folded
// 186 / 4572 / 1381 / 5347 triangles over their neighbours in these steps (the app's own close-up: 0 / 3269 / 1444 / 4972).
// Tendril is left as 0.12.7 had it (#204: its folds sit on the crossing's ridge lips and shade smooth; relaxing them there
// took the count down but scratched the ridge tops, and a flip limiter stops the pull short; with dynamic topology on it folds
// none, tendril_dynamic_topology_session): its steps only guard against getting worse. Palette Knife carries the shape, not the
// vertices (slideBack): on a clean sphere its strokes fold nothing (palette_knife_overlaps_never_fold, palette_knife_big_brush_stack);
// here it crosses the surface Tendril dragged (and folded, #204) first: 8 folds there (618 with the vertices moving), none elsewhere.
// Nudge (limiter for a small brush) a few dozen.
void checkReplaySession() {
    BrushRecipe knife;
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == "Palette Knife") knife = r;
    struct Step { const char* label; const BrushRecipe* recipe; BrushType type; const char* fixture; size_t allowed; };
    const Step steps[] = {{"Tendril", nullptr, BrushType::SnakeHook, "tendril_cross", 250},
                          {"Tendril", nullptr, BrushType::SnakeHook, "tendril_long", 4800},
                          {"Palette Knife", &knife, BrushType::Standard, "knife_cross_overlap", 50},
                          {"Nudge", nullptr, BrushType::Nudge, "nudge_cross", 50}};
    Document d = denseSphere();
    const std::vector<glm::vec3> fresh = d.mesh.positions;
    std::vector<glm::vec3> afterTendril;
    for (const Step& st : steps) {
        const std::vector<glm::vec3> before = d.mesh.positions;
        if (std::string(st.label) == "Palette Knife") afterTendril = before;
        replayStrokes(d, st.recipe, st.type, loadStrokeFixture(st.fixture));
        const size_t folded = foldedFaces(before, d.mesh);
        if (std::string(st.label) == "Palette Knife") { // (its folds, the triangles it moved that lie against a neighbour: on Tendril's?)
            const Mesh& m = d.mesh;
            std::vector<glm::vec3> nrm(m.triangleCount());
            for (uint32_t t = 0; t < m.triangleCount(); ++t) {
                const glm::uvec3 f = m.triangles[t];
                const glm::vec3 c = glm::cross(m.positions[f.y] - m.positions[f.x], m.positions[f.z] - m.positions[f.x]);
                nrm[t] = glm::length(c) > 0.0f ? glm::normalize(c) : glm::vec3(0.0f);
            }
            std::unordered_map<uint64_t, uint32_t> first;
            std::vector<uint8_t> bad(m.triangleCount(), 0);
            for (uint32_t t = 0; t < m.triangleCount(); ++t) {
                const glm::uvec3 f = m.triangles[t];
                const uint32_t c[3] = {f.x, f.y, f.z};
                for (int k = 0; k < 3; ++k) {
                    const uint32_t a = std::min(c[k], c[(k + 1) % 3]), b = std::max(c[k], c[(k + 1) % 3]);
                    const auto [it, isNew] = first.emplace((uint64_t(a) << 32) | b, t);
                    if (!isNew && glm::dot(nrm[t], nrm[it->second]) < -0.5f) bad[t] = bad[it->second] = 1;
                }
            }
            size_t onTendril = 0, elsewhere = 0;
            for (uint32_t t = 0; t < m.triangleCount(); ++t) {
                if (!bad[t]) continue;
                const glm::uvec3 f = m.triangles[t];
                const auto moved = [&](const std::vector<glm::vec3>& a, const std::vector<glm::vec3>& b) {
                    return a[f.x] != b[f.x] || a[f.y] != b[f.y] || a[f.z] != b[f.z];
                };
                if (moved(m.positions, before)) (moved(afterTendril, fresh) ? onTendril : elsewhere)++;
            }
            std::printf("    Palette Knife's folded triangles: %zu on the ground Tendril dragged first, %zu elsewhere\n", onTendril, elsewhere);
            expect(elsewhere == 0, "Palette Knife folded %zu triangles away from Tendril's (dark creases)", elsewhere);
        }
        std::printf("  session %s %s: moved %.4f, %zu triangles folded\n", st.label, st.fixture, maxMove(before, d.mesh), folded);
        expect(nonFinite(d.mesh) == 0, "%s: NaN in %s", st.label, st.fixture);
        expect(folded <= st.allowed, "%s: %zu triangles folded over a neighbour in %s (allowed %zu; dark creases)", st.label, folded,
               st.fixture, st.allowed);
    }
}

[[maybe_unused]] const bool kReplayRegistered = [] {
    addTest("drag_brushes_replay_session", [] { checkReplaySession(); });
    return true;
}();

// Palette Knife on its own, on a fresh dense sphere (#198: creases where two strokes overlap): the close-up session's three
// strokes, then the same three again a third of a brush further down and backwards (six strokes, each over two or more
// dragged areas), X symmetry on, every stroke counted against the mesh before it. No triangle may end folded over a
// neighbour or as a sliver (a long knife stroke used to shear the band along its sides by about 4, into the slivers the next
// stroke turned over). And the knife must still carry the shape as far: after the first two strokes (which the vertex-moving
// drag of 0.13 left unfolded) the surface lies within a fraction of an edge of that drag's, both ways.
float triangleQuality(const Mesh& m, const glm::uvec3& t) { // (1 is equilateral)
    const glm::vec3 e1 = m.positions[t.y] - m.positions[t.x], e2 = m.positions[t.z] - m.positions[t.x];
    const float sum = glm::dot(e1, e1) + glm::dot(e2, e2) + glm::dot(e2 - e1, e2 - e1);
    return sum > 0.0f ? 1.7320508f * glm::length(glm::cross(e1, e2)) / sum : 0.0f;
}
// The distance from p to triangle (a, b, c) (Ericson, Real-Time Collision Detection 5.1.5).
float pointTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return glm::length(ap);
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return glm::length(bp);
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return glm::distance(p, a + ab * (d1 / (d1 - d3)));
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return glm::length(cp);
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return glm::distance(p, a + ac * (d2 / (d2 - d6)));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        return glm::distance(p, b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6))));
    const float den = 1.0f / (va + vb + vc);
    return glm::distance(p, a + ab * (vb * den) + ac * (vc * den));
}
// How far each moved vertex of `a` lies from the surface of `b` (the triangles around b's vertices within two edges of it):
// the median and the 95th percentile.
std::pair<float, float> surfaceGap(const Document& a, const std::vector<glm::vec3>& start, const Document& b, float edge) {
    const float cell = 2.0f * edge;
    const auto key = [cell](const glm::vec3& x) {
        const glm::ivec3 c = glm::ivec3(glm::floor(x / cell));
        return (uint64_t(uint32_t(c.x) & 0x1fffff) << 42) | (uint64_t(uint32_t(c.y) & 0x1fffff) << 21) | uint64_t(uint32_t(c.z) & 0x1fffff);
    };
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    for (uint32_t v = 0; v < b.mesh.vertexCount(); ++v) grid[key(b.mesh.positions[v])].push_back(v);
    std::vector<float> gaps;
    for (uint32_t v = 0; v < a.mesh.vertexCount(); ++v) {
        if (a.mesh.positions[v] == start[v]) continue;
        const glm::vec3 x = a.mesh.positions[v];
        float best = 1e9f;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    auto it = grid.find(key(x + glm::vec3(float(dx), float(dy), float(dz)) * cell));
                    if (it == grid.end()) continue;
                    for (uint32_t u : it->second)
                        for (uint32_t t : b.mesh.vertexTriangles(u)) {
                            const glm::uvec3 f = b.mesh.triangles[t];
                            best = std::min(best, pointTriangle(x, b.mesh.positions[f.x], b.mesh.positions[f.y], b.mesh.positions[f.z]));
                        }
                }
        gaps.push_back(std::min(best, 2.0f * cell)); // (nothing within two cells: counted as two cells off)
    }
    if (gaps.empty()) return {0.0f, 0.0f};
    std::sort(gaps.begin(), gaps.end());
    return {gaps[gaps.size() / 2], gaps[std::min(gaps.size() - 1, size_t(0.95 * double(gaps.size())))]};
}
void checkKnifeOverlaps() {
    BrushRecipe knife;
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == "Palette Knife") knife = r;
    StrokeFixture f = loadStrokeFixture("knife_cross_overlap");
    expect(f.strokes.size() == 3, "knife_cross_overlap: %zu strokes", f.strokes.size());
    for (size_t k = 0; k < 3; ++k) {
        StrokeFixture::One s = f.strokes[k];
        for (glm::vec3& p : s.points) p = glm::normalize(p + glm::vec3(0.0f, -0.33f * f.radius, 0.0f));
        std::reverse(s.points.begin(), s.points.end());
        std::swap(s.p0, s.p1);
        f.strokes.push_back(s);
    }
    const float edge = meanEdge(denseSphereFixture().mesh);
    Document d = denseSphere();
    const std::vector<glm::vec3> start = d.mesh.positions;
    std::vector<glm::vec3> before = start;
    replayStrokes(d, &knife, BrushType::Standard, f, [&](size_t k) {
        const size_t folded = foldedFaces(before, d.mesh), inverted = invertedFaces(before, d.mesh);
        size_t slivers = 0;
        float worst = 1.0f;
        for (const glm::uvec3& t : d.mesh.triangles) {
            const float q = triangleQuality(d.mesh, t);
            slivers += q < 0.2f;
            worst = std::min(worst, q);
        }
        std::printf("  Palette Knife stroke %zu: %zu folded, %zu inside out, %zu slivers (worst triangle %.2f)\n", k + 1, folded,
                    inverted, slivers, worst);
        expect(nonFinite(d.mesh) == 0, "Palette Knife: NaN after stroke %zu", k + 1);
        expect(folded == 0 && inverted == 0, "Palette Knife stroke %zu: %zu triangles folded over a neighbour, %zu inside out",
               k + 1, folded, inverted);
        expect(slivers == 0, "Palette Knife stroke %zu: %zu slivers (quality under 0.2; worst %.3f)", k + 1, slivers, worst);
        before = d.mesh.positions;
    });
    const float sym = symmetryError(d.mesh, sphereMirror());
    std::printf("  Palette Knife: mirror %.3g (%.2f edges)\n", sym, sym / edge);

    // The shape. A drag alone (the knife without its plane) carries the surface where the vertex-moving drag of 0.13 did, on
    // the first stroke without symmetry (that drag left it unfolded; with symmetry the two halves' drags used to add up where
    // they overlap, twice as far, and now blend): the vertices go back across it, the surface stays. With its plane the
    // knife flattens from an even spread of vertices, not the bunched ones that drag left ahead of the tip (they pulled the plane
    // toward the carried, flattened clay): its ridges stand at least as high as before, never softer.
    double ms = 0.0; // (the strokes' own time, the last carry's)
    const auto carry = [&](const BrushRecipe& recipe, bool slideOn, size_t strokes, bool mirror = true) {
        StrokeFixture g = f;
        g.strokes.resize(strokes);
        g.symmetry.x = mirror;
        Document x = denseSphere();
        x.sculptor.dragSlide = slideOn;
        const auto t = std::chrono::steady_clock::now();
        replayStrokes(x, &recipe, BrushType::Standard, g);
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
        return x;
    };
    BrushRecipe drag = knife;
    drag.planeAmount = 0.0f;
    const Document slide = carry(drag, true, 1, false), moving = carry(drag, false, 1, false);
    const auto [midA, p95A] = surfaceGap(slide, start, moving, edge);
    const auto [midB, p95B] = surfaceGap(moving, start, slide, edge);
    float carried = 0.0f;
    for (uint32_t v = 0; v < moving.mesh.vertexCount(); ++v) carried = std::max(carried, glm::distance(moving.mesh.positions[v], start[v]));
    std::printf("  drag alone vs the vertex-moving drag: %.2f / %.2f edges apart (median), %.2f / %.2f (95%%); it carried %.1f edges\n",
                midA / edge, midB / edge, p95A / edge, p95B / edge, carried / edge);
    expect(p95A < 0.5f * edge && p95B < 0.5f * edge, "a drag moved the surface elsewhere than before: %.2f / %.2f edges (95%%)",
           p95A / edge, p95B / edge);
    const auto relief = [](const Mesh& m) {
        float r = 0.0f;
        for (const glm::vec3& x : m.positions) r = std::max(r, std::abs(glm::length(x) - 1.0f));
        return r;
    };
    const float now = relief(carry(knife, true, 2).mesh);
    const double msNow = ms;
    const float old = relief(carry(knife, false, 2).mesh);
    std::printf("  (two knife strokes: %.0f ms, %.0f ms with the vertices moving)\n", msNow, ms);
    std::printf("  Palette Knife relief: %.1f edges (the vertex-moving drag: %.1f)\n", now / edge, old / edge);
    expect(now >= 0.9f * old, "Palette Knife: its ridges stand %.1f edges high, %.1f before (softened)", now / edge, old / edge);
}

[[maybe_unused]] const bool kKnifeRegistered = [] {
    addTest("palette_knife_overlaps_never_fold", [] { checkKnifeOverlaps(); });
    return true;
}();


// Palette Knife with a big brush, strokes stacked on one another (artist-qa, #198: at Size 200, about 0.95 of the unit sphere, the
// first fixes folded tens of thousands of triangles a stroke, and one took longer with every stroke): radius 0.3, 0.7 and 0.95
// (most of the sphere's front; the two mirror halves overlap almost wholly), five strokes crossing, X symmetry. No stroke folds a
// triangle or turns one inside out. Up to 0.7 slivers stay a few dozen of 1.3M (the vertex-moving drag left 190,000); at 0.95
// the plane presses most of the front flat against the way back and many dabs move the vertices with the clay as before
// (slideBack): no more slivers than the vertex-moving drag leaves there, nor folds. The fifth stroke costs about what the first did.
struct StackResult { size_t folded = 0, inverted = 0, slivers = 0; std::vector<double> ms; size_t fallbacks = 0; };
StackResult knifeBigStack(float radius, bool slide, bool everyStroke = true) {
    BrushRecipe knife;
    for (const BrushRecipe& r : builtinBrushRecipes())
        if (r.name == "Palette Knife") knife = r;
    StrokeFixture f;
    f.radius = radius, f.strength = 0.5f, f.symmetry.x = true;
    const glm::vec2 lines[5][2] = {{{-0.44f, 0.2f}, {0.04f, -0.1f}}, {{-0.4f, -0.16f}, {0.0f, 0.16f}}, {{-0.18f, 0.3f}, {-0.18f, -0.36f}},
                                   {{-0.4f, 0.1f}, {0.04f, 0.06f}}, {{-0.34f, -0.2f}, {-0.06f, 0.24f}}};
    const float pressure[5] = {1.0f, 0.5f, 0.8f, 1.0f, 0.7f};
    for (int j = 0; j < 5; ++j) {
        StrokeFixture::One s;
        for (int k = 0; k <= 24; ++k) {
            const glm::vec2 q = glm::mix(lines[j][0], lines[j][1], float(k) / 24.0f);
            s.points.push_back(glm::vec3(q, std::sqrt(std::max(0.0f, 1.0f - glm::dot(q, q)))));
        }
        s.p0 = s.p1 = pressure[j];
        f.strokes.push_back(s);
    }
    Document d = denseSphere();
    d.sculptor.dragSlide = slide;
    const std::vector<glm::vec3> start = d.mesh.positions;
    std::vector<glm::vec3> before = start;
    StackResult out;
    auto t = std::chrono::steady_clock::now();
    replayStrokes(d, &knife, BrushType::Standard, f, [&](size_t k) {
        out.ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count());
        size_t slivers = 0; // (on the triangles the strokes moved so far: the sphere is clean elsewhere)
        for (uint32_t t = 0; t < d.mesh.triangleCount(); ++t) {
            const glm::uvec3 tr = d.mesh.triangles[t];
            if (d.mesh.positions[tr.x] != start[tr.x] || d.mesh.positions[tr.y] != start[tr.y] || d.mesh.positions[tr.z] != start[tr.z])
                slivers += triangleQuality(d.mesh, tr) < 0.2f;
        }
        // (the folds, against the mesh the last count saw: after every stroke, or for the biggest brush, whose fold counts
        // cover most of the sphere, after strokes 1, 3 and 5; none on the vertex-moving drag's comparison run)
        const bool count = slide && (everyStroke || k % 2 == 0);
        const size_t folded = count ? foldedFaces(before, d.mesh) : 0, inverted = count ? invertedFaces(before, d.mesh) : 0;
        std::printf("  Palette Knife%s radius %.2f stroke %zu: %.0f ms, %zu folded, %zu inside out, %zu slivers\n", slide ? "" : " (vertices moving)",
                    radius, k + 1, out.ms.back(), folded, inverted, slivers);
        expect(nonFinite(d.mesh) == 0, "Palette Knife radius %.2f: NaN after stroke %zu", radius, k + 1);
        out.folded += folded, out.inverted += inverted, out.slivers = slivers;
        if (count) before = d.mesh.positions;
        t = std::chrono::steady_clock::now();
    });
    out.fallbacks = d.sculptor.dragSlideFallbacks;
    return out;
}
void checkKnifeBigStack(float radius) {
    const StackResult r = knifeBigStack(radius, true, radius <= 0.7f);
    std::printf("  (radius %.2f: %zu dabs moved the vertices with the clay)\n", radius, r.fallbacks);
    expect(r.folded == 0 && r.inverted == 0, "Palette Knife radius %.2f: %zu folded, %zu inside out over five strokes", radius, r.folded,
           r.inverted);
    if (radius <= 0.7f) {
        expect(r.slivers <= 100, "Palette Knife radius %.2f: %zu slivers (quality under 0.2)", radius, r.slivers);
    } else {
        const StackResult old = knifeBigStack(radius, false);
        expect(r.slivers <= old.slivers, "Palette Knife radius %.2f: %zu slivers (the vertex-moving drag: %zu)", radius, r.slivers, old.slivers);
    }
    expect(r.ms.size() == 5 && r.ms.back() < 3.0 * r.ms.front() + 50.0, "Palette Knife radius %.2f: the fifth stroke took %.0f ms, the first %.0f",
           radius, r.ms.back(), r.ms.front());
}

[[maybe_unused]] const bool kKnifeBigRegistered = [] {
    addTest("palette_knife_big_brush_stack", [] {
        checkKnifeBigStack(0.3f);
        checkKnifeBigStack(0.7f);
    });
    addTest("palette_knife_size200_stack", [] { checkKnifeBigStack(0.95f); }); // (with the vertex-moving drag's run to compare)
    return true;
}();

// Folded pairs of neighbouring triangles anywhere on a mesh (after dynamic topology, whose triangles are not the ones before).
size_t foldedPairs(const Mesh& m) {
    std::unordered_map<uint64_t, uint32_t> first;
    std::vector<glm::vec3> nrm(m.triangleCount());
    size_t n = 0;
    for (uint32_t i = 0; i < m.triangleCount(); ++i) {
        const glm::uvec3 f = m.triangles[i];
        const glm::vec3 a = glm::cross(m.positions[f.y] - m.positions[f.x], m.positions[f.z] - m.positions[f.x]);
        const float l = glm::length(a);
        nrm[i] = l > 0.0f ? a / l : glm::vec3(0.0f);
        const uint32_t c[3] = {f.x, f.y, f.z};
        for (int k = 0; k < 3; ++k) {
            const uint32_t lo = std::min(c[k], c[(k + 1) % 3]), hi = std::max(c[k], c[(k + 1) % 3]);
            const auto [it, added] = first.emplace((uint64_t(lo) << 32) | hi, i);
            if (!added && nrm[i] != glm::vec3(0.0f) && nrm[it->second] != glm::vec3(0.0f) && glm::dot(nrm[i], nrm[it->second]) < -0.5f) ++n;
        }
    }
    return n;
}

// Tendril (#204): the session's crossing and long pull with dynamic topology on fold nothing (the remesher keeps the stretched
// walls sound), and at Size 200 (radius 0.95, artist-qa's) neither stroke folds on a fixed mesh.
[[maybe_unused]] const bool kTendrilRegistered = [] {
    addTest("tendril_dynamic_topology_session", [] {
        Document d = denseSphere();
        d.sculptor.dyntopo.enabled = true, d.sculptor.dyntopo.constantDetail = true, d.sculptor.dyntopo.detail = 0.5f;
        for (const char* fx : {"tendril_cross", "tendril_long"}) {
            replayStrokes(d, nullptr, BrushType::SnakeHook, loadStrokeFixture(fx));
            const size_t folded = foldedPairs(d.mesh);
            std::printf("  dynamic topology %s: %zu triangles, %zu folded pairs\n", fx, d.mesh.triangleCount(), folded);
            expect(nonFinite(d.mesh) == 0, "Tendril: NaN in %s", fx);
            expect(folded == 0, "Tendril: %zu folded pairs with dynamic topology on after %s", folded, fx);
        }
    });
    addTest("tendril_big_brush", [] {
        for (const char* fx : {"tendril_cross", "tendril_long"}) {
            StrokeFixture f = loadStrokeFixture(fx);
            f.radius = 0.95f;
            Document d = denseSphere();
            const std::vector<glm::vec3> start = d.mesh.positions;
            replayStrokes(d, nullptr, BrushType::SnakeHook, f);
            const size_t folded = foldedFaces(start, d.mesh);
            std::printf("  Size 200 %s: moved %.4f, %zu triangles folded\n", fx, maxMove(start, d.mesh), folded);
            expect(folded == 0, "Tendril: %zu triangles folded at radius 0.95 in %s", folded, fx);
        }
    });
    return true;
}();

double sphereVolume(int level) { return meshVolume(makeIcosphere(level)); }

// Distance of every vertex from the unit sphere (the source of the tools below), as a fraction of its radius.
float offSphere(const Mesh& m) {
    float worst = 0.0f;
    for (const glm::vec3& p : m.positions) worst = std::max(worst, std::abs(glm::length(p) - 1.0f));
    return worst;
}

} // namespace

// Smooth keeps the form: on bumps laid by a stroke it takes most of the roughness out, and with Keep volume the
// enclosed volume stays within 0.05% (a plain smooth may shrink, but no more than 0.5%).
TEST(smooth_invariant_keeps_volume_and_removes_bumps) {
    guarded("smooth_invariant_keeps_volume_and_removes_bumps", [] {
        for (int keep = 0; keep < 2; ++keep) {
            const glm::vec3 at = glm::normalize(glm::vec3(0.3f, 0.7f, 0.6f));
            Document d = roughSphere(at);
            const std::vector<glm::vec3> bumped = d.mesh.positions;
            const double v0 = meshVolume(d.mesh);
            const float edge = meanEdge(denseSphereFixture().mesh);
            auto roughness = [&](const Mesh& m) {
                double sum = 0.0;
                int n = 0;
                for (uint32_t v = 0; v < m.vertexCount(); ++v) {
                    if (glm::distance(bumped[v], at) > 0.12f) continue;
                    glm::vec3 mean(0.0f);
                    auto ring = m.neighbors(v);
                    for (uint32_t u : ring) mean += m.positions[u];
                    sum += glm::length(m.positions[v] - mean / float(ring.size()));
                    ++n;
                }
                return n ? sum / n : 0.0;
            };
            const double r0 = roughness(d.mesh);
            std::vector<DabParams> smooth;
            for (int i = 0; i < 6; ++i) {
                DabParams p;
                p.center = at, p.viewDir = -at, p.radius = 0.15f, p.strength = 1.0f, p.keepVolume = keep;
                smooth.push_back(p);
            }
            strokes(d, "Smooth", [](Document& x, const DabParams& p, Symmetry s) {
                x.sculptor.dab(x.mesh, x.bvh, BrushType::Smooth, p, s);
            }, {smooth}, kNoSymmetry);
            const double r1 = roughness(d.mesh), dv = volumeChange(v0, meshVolume(d.mesh));
            const size_t flips = flippedFaces(bumped, d.mesh);
            std::printf("  smooth%s: roughness %.3g -> %.3g edges, volume %+.4f%%, flips %zu\n", keep ? " (keep volume)" : "",
                        r0 / edge, r1 / edge, 100.0 * dv, flips);
            expect(r1 < 0.5 * r0, "Smooth%s left the bumps (roughness %.2f -> %.2f edges)", keep ? " (keep volume)" : "",
                   r0 / edge, r1 / edge);
            expect(std::abs(dv) < (keep ? 0.0005 : 0.005), "Smooth%s changed the volume by %.3f%%",
                   keep ? " (keep volume)" : "", 100.0 * dv);
            expect(flips == 0 && nonFinite(d.mesh) == 0, "Smooth: %zu flipped triangles or NaN", flips);
        }
    });
}

// Mask: painting a mask moves nothing, stays in 0..1 and mirrors with symmetry; a stroke over it leaves fully
// masked vertices exactly where they were.
TEST(mask_invariant_moves_nothing_and_holds_masked) {
    guarded("mask_invariant_moves_nothing_and_holds_masked", [] {
        Document d = denseSphere();
        const std::vector<glm::vec3> before = d.mesh.positions;
        const glm::vec3 at = glm::normalize(glm::vec3(0.45f, 0.6f, 0.66f));
        for (const auto& s : overlapStrokes(at, 0.15f, 1.0f)) {
            d.sculptor.beginStroke(d.mesh, "Mask");
            for (const DabParams& p : s) d.sculptor.dabMask(d.mesh, d.bvh, p, kMirrorX);
            d.undo.push(d.sculptor.endStroke());
        }
        const std::vector<uint32_t>& map = sphereMirror();
        float lo = 1.0f, hi = 0.0f, asym = 0.0f;
        int full = 0;
        for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
            const float m = d.mesh.mask.empty() ? 0.0f : d.mesh.mask[v];
            lo = std::min(lo, m), hi = std::max(hi, m), full += m > 0.99f;
            if (map[v] != ~0u && !d.mesh.mask.empty()) asym = std::max(asym, std::abs(m - d.mesh.mask[map[v]]));
        }
        std::printf("  mask: range %.3f..%.3f, %d fully masked, mirror diff %.3g, moved %.3g\n", lo, hi, full, asym,
                    maxMove(before, d.mesh));
        expect(maxMove(before, d.mesh) == 0.0f, "painting a mask moved the surface");
        expect(lo >= 0.0f && hi <= 1.0f && hi > 0.999f && full > 100, "the mask left 0..1 or masked nothing (%.3f..%.3f, %d full)", lo, hi,
               full);
        expect(asym < 1e-4f, "the mask isn't mirrored (differs by %.3g across X)", asym);
        const std::vector<glm::vec3> masked = d.mesh.positions;
        const auto over = overlapStrokes(at, 0.2f, 1.0f);
        const Dabber standard = [](Document& x, const DabParams& p, Symmetry s) {
            x.sculptor.dab(x.mesh, x.bvh, BrushType::Standard, p, s);
        };
        Document free = denseSphere(); // (the same stroke without the mask)
        strokes(free, "Standard", standard, over, kNoSymmetry);
        strokes(d, "Standard", standard, over, kNoSymmetry);
        // Each vertex moves at most (1 - mask) of what the same stroke moves it without the mask (+ 2% of it: the
        // per-dab cap rounds), so a fully masked one stays.
        float excess = 0.0f;
        for (uint32_t v = 0; v < d.mesh.vertexCount() && !d.mesh.mask.empty(); ++v) {
            const float m = d.mesh.mask[v];
            if (m <= 0.0f) continue;
            const float own = glm::distance(free.mesh.positions[v], masked[v]);
            const float got = glm::distance(d.mesh.positions[v], masked[v]);
            excess = std::max(excess, got - ((1.0f - m) * own + 0.02f * own + 1e-7f));
        }
        std::printf("  stroke over the mask: largest move %.4g, worst excess over (1 - mask) %.3g\n", maxMove(masked, d.mesh),
                    excess);
        expect(excess <= 0.0f, "a stroke moved partly masked vertices %.3g more than their mask lets it", excess);
        expect(maxMove(masked, d.mesh) > 1e-4f, "the stroke around the mask moved nothing");
    });
}

// Voxel remesh: closed and manifold, every face outwards, on the form (within a voxel) and its volume (1%).
TEST(voxel_remesh_invariant_closed_on_the_form) {
    guarded("voxel_remesh_invariant_closed_on_the_form", [] {
        const Mesh src = makeIcosphere(6);
        std::string err;
        const Mesh r = voxelRemesh(src, 128, &err);
        const double dv = volumeChange(meshVolume(src), meshVolume(r));
        std::printf("  voxel remesh: %zu tris, watertight %d, inward %zu, off sphere %.4f, volume %+.3f%%\n",
                    r.triangleCount(), int(isWatertight(r)), inwardFaces(r), offSphere(r), 100.0 * dv);
        expect(r.triangleCount() > 1000 && err.empty(), "voxel remesh failed: %s", err.c_str());
        expect(isWatertight(r), "voxel remesh left holes or non-manifold edges");
        expect(inwardFaces(r) == 0 && nonFinite(r) == 0, "voxel remesh: %zu faces inside out or NaN", inwardFaces(r));
        expect(offSphere(r) < 2.0f * 2.0f / 128.0f, "voxel remesh strayed %.4f off the form (a voxel is %.4f)", offSphere(r),
               2.0f / 128.0f);
        expect(std::abs(dv) < 0.01, "voxel remesh changed the volume by %.2f%%", 100.0 * dv);
    });
}

// Quad remesh: quads, closed and manifold, on the form and its volume (1%).
TEST(quad_remesh_invariant_closed_quads_on_the_form) {
    guarded("quad_remesh_invariant_closed_quads_on_the_form", [] {
        const Mesh src = makeIcosphere(5);
        QRemeshOptions o;
        o.targetQuads = 1500;
        Mesh out;
        std::string err;
        QRemeshStats st;
        const bool ok = qremesh(src, o, out, err, {}, &st);
        const TopologyReport t = topologyReport(out);
        const double dv = volumeChange(meshVolume(src), meshVolume(out));
        std::printf("  quad remesh: %zu faces (%.1f%% quads), boundary %zu, non-manifold %zu, off sphere %.4f, volume %+.3f%%\n",
                    t.faces, 100.0f * t.quadFraction(), t.boundaryEdges, t.nonManifoldEdges, offSphere(out), 100.0 * dv);
        expect(ok, "quad remesh failed: %s", err.c_str());
        expect(t.manifold() && t.boundaryEdges == 0, "quad remesh left holes or non-manifold edges");
        expect(t.quadFraction() > 0.95f, "quad remesh left %.1f%% triangles", 100.0f * (1.0f - t.quadFraction()));
        expect(inwardFaces(out) == 0 && nonFinite(out) == 0, "quad remesh: faces inside out or NaN");
        expect(offSphere(out) < 0.01f, "quad remesh strayed %.4f off the form", offSphere(out));
        expect(std::abs(dv) < 0.01, "quad remesh changed the volume by %.2f%%", 100.0 * dv);
    });
}

// Quad remesh on a dense grainy sphere (gh#226: grain was taken for hard edges and tips, the layout never finished):
// closed quads on the form (within the grain and a little), the volume (1%), faces outwards, in bounded time; and a
// cancelled run writes nothing.
TEST(quad_remesh_invariant_grainy_sphere_closed_on_the_form) {
    guarded("quad_remesh_invariant_grainy_sphere_closed_on_the_form", [] {
        Mesh src = makeIcosphere(7); // 327k triangles: the dense path
        src.recomputeAllNormals();
        const float grain = 0.004f; // along the normals, up to 0.4% of the radius either way
        uint32_t state = 12345u;
        for (size_t v = 0; v < src.positions.size(); ++v) {
            state = state * 1664525u + 1013904223u;
            src.positions[v] += src.normals[v] * (grain * (2.0f * float(state >> 8) / float(1u << 24) - 1.0f));
        }
        src.recomputeAllNormals();
        QRemeshOptions o;
        o.targetQuads = 4000;
        Mesh out;
        std::string err;
        int calls = 0;
        expect(!qremesh(src, o, out, err, [&](float) { return ++calls < 40; }) && out.triangleCount() == 0,
               "a cancelled quad remesh wrote a result");
        QRemeshStats st;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = qremesh(src, o, out, err, {}, &st);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const TopologyReport t = topologyReport(out);
        const double dv = volumeChange(meshVolume(src), meshVolume(out));
        std::printf(
            "  grainy quad remesh: %zu faces (%.1f%% quads), non-manifold %zu, boundary %zu, off sphere %.4f, volume "
            "%+.3f%%, %.1f s\n",
            t.faces, 100.0f * t.quadFraction(), t.nonManifoldEdges, t.boundaryEdges, offSphere(out), 100.0 * dv, secs);
        expect(ok, "quad remesh failed: %s", err.c_str());
        expect(t.manifold() && t.boundaryEdges == 0, "quad remesh left holes or non-manifold edges");
        expect(t.quadFraction() > 0.95f, "quad remesh left %.1f%% triangles", 100.0f * (1.0f - t.quadFraction()));
        expect(inwardFaces(out) == 0 && nonFinite(out) == 0, "quad remesh: faces inside out or NaN");
        expect(offSphere(out) < grain + 0.01f, "quad remesh strayed %.4f off the form", offSphere(out));
        expect(std::abs(dv) < 0.01, "quad remesh changed the volume by %.2f%%", 100.0 * dv);
        expect(secs < timeLimit(20.0), "quad remesh took %.1f s on 327k grainy triangles", secs);
    });
}

// Decimate: a tenth of the triangles, still closed, faces outwards, on the form and its volume (0.5%).
TEST(decimate_invariant_closed_on_the_form) {
    guarded("decimate_invariant_closed_on_the_form", [] {
        const Mesh src = makeIcosphere(7);
        const Mesh r = decimate(src, src.triangleCount() / 10);
        const double dv = volumeChange(meshVolume(src), meshVolume(r));
        std::printf("  decimate: %zu -> %zu tris, watertight %d, inward %zu, off sphere %.4f, volume %+.3f%%\n",
                    src.triangleCount(), r.triangleCount(), int(isWatertight(r)), inwardFaces(r), offSphere(r), 100.0 * dv);
        expect(r.triangleCount() <= src.triangleCount() / 9, "decimate kept %zu triangles", r.triangleCount());
        expect(isWatertight(r), "decimate opened holes or non-manifold edges");
        expect(inwardFaces(r) == 0 && nonFinite(r) == 0, "decimate: %zu faces inside out or NaN", inwardFaces(r));
        expect(offSphere(r) < 0.01f, "decimate strayed %.4f off the form", offSphere(r));
        expect(std::abs(dv) < 0.005, "decimate changed the volume by %.2f%%", 100.0 * dv);
    });
}

// Booleans: a sphere minus another one unit off is closed and has the lens's volume taken out (1%).
TEST(boolean_invariant_subtract_closed_with_the_right_volume) {
    guarded("boolean_invariant_subtract_closed_with_the_right_volume", [] {
        const Mesh a = makeIcosphere(6);
        Mesh b = makeIcosphere(6);
        constexpr float kD = 1.0f;
        for (glm::vec3& p : b.positions) p.x += kD;
        b.recomputeAllNormals();
        Mesh out;
        std::string err;
        BooleanInfo info;
        const bool ok = booleanMeshes(a, b, BooleanOp::Subtract, out, err, {}, &info);
        const double lens = 3.14159265358979 * (4.0 + kD) * (2.0 - kD) * (2.0 - kD) / 12.0; // (two unit spheres)
        const double want = sphereVolume(6) * (1.0 - lens / (4.0 / 3.0 * 3.14159265358979));
        const double dv = volumeChange(want, meshVolume(out));
        std::printf("  boolean subtract: ok %d (voxel %d), watertight %d, volume %+.3f%% of the expected\n", int(ok),
                    int(info.voxel), int(isWatertight(out)), 100.0 * dv);
        expect(ok, "boolean subtract failed: %s", err.c_str());
        expect(isWatertight(out), "boolean subtract left holes or non-manifold edges");
        expect(nonFinite(out) == 0 && meshVolume(out) > 0.0, "boolean subtract: NaN or the result faces inwards");
        expect(std::abs(dv) < 0.01, "boolean subtract's volume is off by %.2f%%", 100.0 * dv);
    });
}

// Cut (Rectangle, Away): the part outside x > 0.4 goes, the rest is closed with a flat cap, nothing past the cut
// remains, and the kept surface is exactly where it was.
TEST(cut_invariant_closed_cap_and_kept_surface_untouched) {
    guarded("cut_invariant_closed_cap_and_kept_surface_untouched", [] {
        const Mesh src = makeIcosphere(6);
        const glm::mat4 vp = glm::ortho(-2.0f, 2.0f, -2.0f, 2.0f, 0.1f, 10.0f) *
                             glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0.0f), glm::vec3(0, 1, 0));
        std::vector<glm::dvec2> region;
        std::string err;
        expect(cutRegion(CutShape::Rect, {{0.2f, -0.9f}, {0.9f, 0.9f}}, false, false, region, err), "cut region: %s",
               err.c_str());
        Mesh out;
        const bool ok = cutMesh(src, vp, region, CutMode::Away, 0.0f, out, err);
        float past = -1.0f, offKept = 0.0f;
        for (const glm::vec3& p : out.positions) {
            past = std::max(past, p.x - 0.4f);
            if (p.x < 0.39f) offKept = std::max(offKept, std::abs(glm::length(p) - 1.0f));
        }
        const double cap = 3.14159265358979 * 0.6 * 0.6 * (3.0 - 0.6) / 3.0; // (the cap past x = 0.4)
        const double dv = volumeChange(sphereVolume(6) - cap, meshVolume(out));
        std::printf("  cut: ok %d, watertight %d, past the cut %.2g, kept surface off %.2g, volume %+.3f%%\n", int(ok),
                    int(isWatertight(out)), past, offKept, 100.0 * dv);
        expect(ok, "cut failed: %s", err.c_str());
        expect(isWatertight(out), "the cut left holes or non-manifold edges");
        expect(past < 1e-4f, "the cut left surface %.3g past its line", past);
        expect(offKept < 1e-4f, "the cut moved the kept surface by %.3g", offKept);
        expect(std::abs(dv) < 0.01, "the cut's volume is off by %.2f%%", 100.0 * dv);
    });
}

// Dynamic topology: a raising stroke on a sphere keeps it closed and manifold, faces outwards, nothing sinks into
// the form (beyond an edge's chord sag), nothing NaN, and the new edges are near the asked length.
TEST(dyntopo_invariant_closed_outward_build_only) {
    guarded("dyntopo_invariant_closed_outward_build_only", [] {
        Document d = makeDoc(makeIcosphere(5));
        d.sculptor.dyntopo.enabled = true;
        d.sculptor.dyntopo.detail = 0.7f;
        const glm::vec3 at = glm::normalize(glm::vec3(0.45f, 0.6f, 0.66f));
        strokes(d, "Standard", [](Document& x, const DabParams& p, Symmetry s) {
            x.sculptor.dab(x.mesh, x.bvh, BrushType::Standard, p, s);
        }, overlapStrokes(at, 0.2f, 0.6f), kMirrorX);
        float inside = 0.0f;
        for (const glm::vec3& p : d.mesh.positions) inside = std::max(inside, 1.0f - glm::length(p));
        const float edge = meanEdge(makeIcosphere(5));
        const Mesh& m = d.mesh;
        std::printf("  dyntopo: %zu tris, watertight %d, inward %zu, deepest inside %.4f (edge %.4f)\n", m.triangleCount(),
                    int(isWatertight(m)), inwardFaces(m), inside, edge);
        expect(isWatertight(m), "dynamic topology left holes or non-manifold edges");
        expect(inwardFaces(m) == 0 && nonFinite(m) == 0, "dynamic topology: %zu faces inside out or NaN", inwardFaces(m));
        expect(inside < 0.25f * edge, "a raising stroke with dynamic topology sank the surface %.3g", inside);
    });
}

// A big Clay dab on a curved form builds a mound, highest in the middle, not a ring: its profile never climbs
// going outward, at a small radius and at half the form's radius (#196: rings per dab on big strokes).
TEST(clay_big_brush_invariant_builds_a_mound_not_a_ring) {
    guarded("clay_big_brush_invariant_builds_a_mound_not_a_ring", [] {
        for (float radius : {0.15f, 0.5f}) {
            float peak = 0.0f;
            const Profile pr = oneDab(BrushType::Clay, radius, peak);
            std::printf("  Clay radius %.2f: peak %.4f, %d climbing steps (worst %.1f%% of the peak); profile", radius, peak,
                        pr.steps, 100.0f * pr.worstClimb);
            for (size_t i = 0; i < pr.h.size(); i += std::max<size_t>(1, pr.h.size() / 8)) std::printf(" %.4f", pr.h[i]);
            std::printf("\n");
            expect(peak > 0.0f, "Clay at radius %.2f built nothing", radius);
            expect(pr.steps == 0, "Clay at radius %.2f lifts a ring: its profile climbs %.1f%% of its height toward the edge",
                   radius, 100.0f * pr.worstClimb);
        }
    });
}

// gh#184 (Sharp Crease on a dense lip, X on): "one stroke was triggering other strokes in adjacent areas ... I did
// undo a few strokes before". Side-by-side crease lines, two undone, then new lines where they were and beside them:
// undo is exact, and every vertex a new stroke moves is inside one of its dabs or one of their X mirrors.
TEST(sharp_crease_after_undo_moves_only_its_dabs_and_mirror) {
    guarded("sharp_crease_after_undo_moves_only_its_dabs_and_mirror", [] {
        Document d = denseSphere();
        constexpr float kRadius = 0.05f;
        auto line = [&](float x, std::vector<glm::vec3>* centers) {
            d.sculptor.beginStroke(d.mesh, "Sharp Crease");
            for (int i = 0; i < 12; ++i) {
                DabParams p;
                p.center = glm::normalize(glm::vec3(x, 0.3f - 0.05f * float(i), 1.0f));
                p.viewDir = -p.center;
                p.radius = kRadius, p.strength = 0.6f, p.pressure = 0.4f + 0.05f * float(i);
                p.strokeDelta = glm::vec3(0.0f, -0.05f, 0.0f);
                d.sculptor.dab(d.mesh, d.bvh, BrushType::SharpCrease, p, kMirrorX);
                if (centers) centers->push_back(p.center), centers->push_back(p.center * glm::vec3(-1.0f, 1.0f, 1.0f));
            }
            d.undo.push(d.sculptor.endStroke());
        };
        line(0.10f, nullptr);
        const std::vector<glm::vec3> afterFirst = d.mesh.positions;
        line(0.16f, nullptr);
        line(0.22f, nullptr);
        CHECK(d.undo.undo(d.mesh, d.bvh) && d.undo.undo(d.mesh, d.bvh));
        CHECK(d.mesh.positions == afterFirst);
        for (const float x : {0.16f, 0.13f, 0.22f}) {
            const std::vector<glm::vec3> before = d.mesh.positions;
            std::vector<glm::vec3> centers;
            line(x, &centers);
            size_t moved = 0, outside = 0;
            float far = 0.0f;
            for (uint32_t v = 0; v < d.mesh.vertexCount(); ++v) {
                if (d.mesh.positions[v] == before[v]) continue;
                ++moved;
                float nearest = 1e30f;
                for (const glm::vec3& c : centers) nearest = std::min(nearest, glm::distance(before[v], c));
                if (nearest > kRadius * 1.02f) ++outside, far = std::max(far, nearest / kRadius);
            }
            std::printf("    x=%.2f: %zu moved, %zu outside the dabs and their mirrors (farthest %.2f radii)\n", double(x), moved,
                        outside, double(far));
            CHECK(moved > 0);
            CHECK(outside == 0);
        }
        // The three new ones undone and redone: the same positions.
        const std::vector<glm::vec3> top = d.mesh.positions;
        for (int i = 0; i < 3; ++i) CHECK(d.undo.undo(d.mesh, d.bvh));
        CHECK(d.mesh.positions == afterFirst);
        for (int i = 0; i < 3; ++i) CHECK(d.undo.redo(d.mesh, d.bvh));
        CHECK(d.mesh.positions == top);
    });
}
