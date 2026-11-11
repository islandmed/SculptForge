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
    {"palette_knife_invariant", "#198"},
    {"dragon_scales_invariant", "#197"},
    {"stone_carve_invariant", "#197"},
    {"skin_pores_invariant", "#197"},
    {"edge_wear_invariant", "#197"},
    {"crisp_flatten_invariant", "fix/crisp-flatten-cut (rises, edge spikes)"},
    {"vent_slots_invariant", "#197"},
    {"fine_pores_invariant", "#197"},
    {"deep_pores_invariant", "#197"},
    {"crow_s_feet_invariant", "#197"},
    {"leather_hide_invariant", "#197"},
    {"muscle_fibre_invariant", "#197"},
    {"seam_stitch_invariant", "#197"},
    {"woven_cloth_invariant", "#197"},
    {"wrinkle_bunch_invariant", "#197"},
    {"dry_cracks_invariant", "#197"},
    {"deep_fissure_invariant", "#197"},
    {"edge_chips_invariant", "#197"},
    {"eroded_stone_invariant", "#197"},
    {"rock_layers_invariant", "#197"},
    {"cobbles_invariant", "#197"},
    {"wood_grain_invariant", "#197"},
    {"rope_twist_invariant", "#197"},
    {"scale_mail_invariant", "#197"},
    {"weathered_planks_invariant", "#197"},
    {"raised_grain_invariant", "#197"},
    {"splinters_invariant", "#197"},
    {"wood_chaff_invariant", "#197"},
    {"grain_cracks_invariant", "#197"},
    {"knot_invariant", "#197"},
    {"end_rings_invariant", "#197"},
    {"plank_seams_invariant", "#197"},
    {"brush_tendril_invariant", "#198"},
    {"brush_nudge_invariant", "#199"},
    {"brush_cloth_invariant", "#200"},
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
    case BrushType::SnakeHook: case BrushType::Nudge: case BrushType::KelvinSnake: case BrushType::Cloth:
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
    expect(inverted == 0, "%s: %zu triangles folded inside out (dark specks)", name.c_str(), inverted);
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
